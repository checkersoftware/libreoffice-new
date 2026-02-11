/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <sal/config.h>

#include <unx/geninst.h>
#include <font/PhysicalFontCollection.hxx>
#include <font/fontsubstitution.hxx>
#include <unx/fontmanager.hxx>
#include <unotools/fontdefs.hxx>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <unordered_set>
#include <functional>
#include <cstdlib>
#include <unx/freetype_glyphcache.hxx>
#include <sal/log.hxx>
#endif

// platform specific font substitution hooks

namespace {

class FcPreMatchSubstitution
:   public vcl::font::PreMatchFontSubstitution
{
public:
    bool FindFontSubstitute( vcl::font::FontSelectPattern& ) const override;
    typedef ::std::pair<vcl::font::FontSelectPattern, vcl::font::FontSelectPattern> value_type;
private:
    typedef ::std::list<value_type> CachedFontMapType;
    mutable CachedFontMapType maCachedFontMap;
};

class FcGlyphFallbackSubstitution
:    public vcl::font::GlyphFallbackFontSubstitution
{
    // TODO: add a cache
public:
    bool FindFontSubstitute(vcl::font::FontSelectPattern&, LogicalFontInstance* pLogicalFont, OUString& rMissingCodes) const override;
};

}

#ifdef EMSCRIPTEN

// Static pointer to PhysicalFontCollection, set during RegisterFontSubstitutors().
// Lifetime matches the application -- the collection is never rebuilt.
static vcl::font::PhysicalFontCollection* s_pFontCollection = nullptr;

// Negative cache: fonts that JS reported as unavailable or that failed registration.
// Keyed by family name + style hints so Phase 3 variant support works automatically.
struct WasmFontCacheKey
{
    OUString maFamilyName;
    FontWeight meWeight;
    FontItalic meItalic;
    FontWidth meWidthType;
    FontPitch mePitch;

    bool operator==(const WasmFontCacheKey& rOther) const
    {
        return maFamilyName == rOther.maFamilyName
            && meWeight == rOther.meWeight
            && meItalic == rOther.meItalic
            && meWidthType == rOther.meWidthType
            && mePitch == rOther.mePitch;
    }
};

struct WasmFontCacheKeyHash
{
    size_t operator()(const WasmFontCacheKey& rKey) const
    {
        size_t nHash = rKey.maFamilyName.hashCode();
        nHash ^= std::hash<int>()(static_cast<int>(rKey.meWeight)) + 0x9e3779b9 + (nHash << 6) + (nHash >> 2);
        nHash ^= std::hash<int>()(static_cast<int>(rKey.meItalic)) + 0x9e3779b9 + (nHash << 6) + (nHash >> 2);
        nHash ^= std::hash<int>()(static_cast<int>(rKey.meWidthType)) + 0x9e3779b9 + (nHash << 6) + (nHash >> 2);
        nHash ^= std::hash<int>()(static_cast<int>(rKey.mePitch)) + 0x9e3779b9 + (nHash << 6) + (nHash >> 2);
        return nHash;
    }
};

static std::unordered_set<WasmFontCacheKey, WasmFontCacheKeyHash> s_aWasmNegativeCache;

// Data shared between the calling pthread and the main-thread proxy callback.
struct FontResolveRequest
{
    const char* pFamilyName;   // input: font family name (UTF-8)
    char* pResultPath;         // output: VFS path (caller must free) or nullptr
    em_proxying_ctx* pCtx;     // set by the proxy callback
};

// Starts async font resolution on the main thread. When the Promise resolves
// (or rejects), calls _em_fontResolveComplete which signals the waiting pthread.
// The resolver callback is stored on globalThis.__resolveSystemFont by the
// Electron app (libreoffice-wasm.ts).
EM_JS(void, em_startFontResolve, (const char* pFamilyName, void* pReq), {
    var familyName = UTF8ToString(pFamilyName);
    var resolver = globalThis.__resolveSystemFont;

    if (!resolver) {
        console.warn('em_startFontResolve: no resolver available');
        _em_fontResolveComplete(pReq, 0);
        return;
    }

    resolver(familyName).then(function(fontData) {
        if (!fontData || fontData.byteLength === 0) {
            _em_fontResolveComplete(pReq, 0);
            return;
        }
        var safeName = familyName.replace(/[^a-zA-Z0-9_-]/g, '_');
        var path = '/tmp/fonts/' + safeName + '.ttf';
        try { FS.mkdirTree('/tmp/fonts'); } catch(e) {}
        FS.writeFile(path, new Uint8Array(fontData));
        console.warn('em_startFontResolve: wrote font to', path);
        _em_fontResolveComplete(pReq, stringToNewUTF8(path));
    }).catch(function(e) {
        console.warn('em_startFontResolve: failed for ' + familyName, e);
        _em_fontResolveComplete(pReq, 0);
    });
});

// Called by JS when async font resolution completes.
// Stores the result path and signals the waiting pthread via proxy_finish.
extern "C" EMSCRIPTEN_KEEPALIVE
void em_fontResolveComplete(void* pReq, char* pPath)
{
    auto* pRequest = static_cast<FontResolveRequest*>(pReq);
    pRequest->pResultPath = pPath;
    emscripten_proxy_finish(pRequest->pCtx);
}

// Proxy callback: runs on the main thread, kicks off async font resolution.
// Does NOT call emscripten_proxy_finish -- the JS completion handler does that.
static void fontResolveOnMainThread(em_proxying_ctx* ctx, void* pArg)
{
    auto* pRequest = static_cast<FontResolveRequest*>(pArg);
    pRequest->pCtx = ctx;
    em_startFontResolve(pRequest->pFamilyName, pArg);
}

// Resolve a font from the host JS environment. Blocks the calling pthread
// until the main thread completes the async resolution.
// Returns a VFS path as a C string (caller must free), or nullptr.
static char* resolveFontFromHost(const char* pFamilyName)
{
    FontResolveRequest aRequest;
    aRequest.pFamilyName = pFamilyName;
    aRequest.pResultPath = nullptr;
    aRequest.pCtx = nullptr;

    emscripten_proxy_sync_with_ctx(
        emscripten_proxy_get_system_queue(),
        emscripten_main_runtime_thread_id(),
        fontResolveOnMainThread,
        &aRequest);

    return aRequest.pResultPath;
}

// Register a font file from the VFS with the font pipeline.
// Replicates the logic of FreeTypeTextRenderImpl::AddTempDevFont()
// using singletons (PrintFontManager, FreetypeManager) and the stored
// PhysicalFontCollection pointer. Returns true on success.
static bool registerFontFromVFS(const OUString& rFileURL, const OUString& rFontName)
{
    if (!s_pFontCollection)
        return false;

    psp::PrintFontManager& rMgr = psp::PrintFontManager::get();
    std::vector<psp::fontID> aFontIds = rMgr.addFontFile(rFileURL);
    if (aFontIds.empty())
        return false;

    FreetypeManager& rFreetypeManager = FreetypeManager::get();
    for (auto const& nFontId : aFontIds)
    {
        auto const* pFont = rMgr.getFont(nFontId);
        if (!pFont)
            continue;

        FontAttributes aDFA = pFont->m_aFontAttributes;
        aDFA.IncreaseQualityBy(5800);
        if (!rFontName.isEmpty())
            aDFA.SetFamilyName(rFontName);

        int nFaceNum = rMgr.getFontFaceNumber(nFontId);
        int nVariantNum = rMgr.getFontFaceVariation(nFontId);

        const OString aFileName = rMgr.getFontFileSysPath(nFontId);
        rFreetypeManager.AddFontFile(aFileName, nFaceNum, nVariantNum, nFontId, aDFA);
    }

    rFreetypeManager.AnnounceFonts(s_pFontCollection);
    return true;
}

#endif // EMSCRIPTEN

void SalGenericInstance::RegisterFontSubstitutors(vcl::font::PhysicalFontCollection* pFontCollection)
{
#ifdef EMSCRIPTEN
    s_pFontCollection = pFontCollection;
#endif

    // register font fallback substitutions
    static FcPreMatchSubstitution aSubstPreMatch;
    pFontCollection->SetPreMatchHook( &aSubstPreMatch );

    // register glyph fallback substitutions
    static FcGlyphFallbackSubstitution aSubstFallback;
    pFontCollection->SetFallbackHook( &aSubstFallback );
}

static vcl::font::FontSelectPattern GetFcSubstitute(const vcl::font::FontSelectPattern &rFontSelData, OUString& rMissingCodes)
{
    vcl::font::FontSelectPattern aSubstituted(rFontSelData);
    psp::PrintFontManager& rMgr = psp::PrintFontManager::get();
    rMgr.Substitute(aSubstituted, rMissingCodes);
    return aSubstituted;
}

namespace
{
    bool uselessmatch(const vcl::font::FontSelectPattern &rOrig, const vcl::font::FontSelectPattern &rNew)
    {
        return
          (
            rOrig.maTargetName == rNew.maSearchName &&
            rOrig.GetWeight() == rNew.GetWeight() &&
            rOrig.GetItalic() == rNew.GetItalic() &&
            rOrig.GetPitch() == rNew.GetPitch() &&
            rOrig.GetWidthType() == rNew.GetWidthType()
          );
    }

    class equal
    {
    private:
        const vcl::font::FontSelectPattern& mrAttributes;
    public:
        explicit equal(const vcl::font::FontSelectPattern& rAttributes)
            : mrAttributes(rAttributes)
        {
        }
        bool operator()(const FcPreMatchSubstitution::value_type& rOther) const
            { return rOther.first == mrAttributes; }
    };
}

bool FcPreMatchSubstitution::FindFontSubstitute(vcl::font::FontSelectPattern &rFontSelData) const
{
    // We don't actually want to talk to Fontconfig at all for symbol fonts
    if( rFontSelData.IsMicrosoftSymbolEncoded() )
        return false;
    // OpenSymbol is a unicode font, but it still deserves to be treated as a symbol font
    if ( IsOpenSymbol(rFontSelData.maSearchName) )
        return false;

    //see fdo#41556 and fdo#47636
    //fontconfig can return e.g. an italic font for a non-italic input and/or
    //different fonts depending on fontsize, bold, etc settings so don't cache
    //just on the name, cache map all the input and all the output not just map
    //from original selection to output fontname
    vcl::font::FontSelectPattern& rPatternAttributes = rFontSelData;
    CachedFontMapType &rCachedFontMap = maCachedFontMap;
    CachedFontMapType::iterator itr = std::find_if(rCachedFontMap.begin(), rCachedFontMap.end(), equal(rPatternAttributes));
    if (itr != rCachedFontMap.end())
    {
        // Cached substitution
        rFontSelData = itr->second;
        if (itr != rCachedFontMap.begin())
        {
            // MRU, move it to the front
            rCachedFontMap.splice(rCachedFontMap.begin(), rCachedFontMap, itr);
        }
        return true;
    }

#ifdef EMSCRIPTEN
    if (s_pFontCollection
        && !s_pFontCollection->FindFontFamily(rFontSelData.maTargetName))
    {
        WasmFontCacheKey aCacheKey{
            rFontSelData.maTargetName,
            rFontSelData.GetWeight(),
            rFontSelData.GetItalic(),
            rFontSelData.GetWidthType(),
            rFontSelData.GetPitch()
        };

        if (s_aWasmNegativeCache.count(aCacheKey))
        {
            SAL_INFO("vcl.fonts", "WASM font cache: negative hit for \""
                     << rFontSelData.maTargetName
                     << "\", skipping JS call");
            // Fall through to fontconfig substitution
        }
        else
        {
            SAL_INFO("vcl.fonts", "WASM font resolution: requesting \""
                     << rFontSelData.maTargetName << "\" from host"
                     << " (w=" << rFontSelData.GetWeight()
                     << " i=" << rFontSelData.GetItalic()
                     << " wd=" << rFontSelData.GetWidthType()
                     << " p=" << rFontSelData.GetPitch() << ")");

            OString aUtf8 = OUStringToOString(rFontSelData.maTargetName,
                                               RTL_TEXTENCODING_UTF8);
            char* pPath = resolveFontFromHost(aUtf8.getStr());
            if (pPath)
            {
                OUString aPath = OStringToOUString(
                    OString(pPath), RTL_TEXTENCODING_UTF8);
                free(pPath);

                OUString aFileURL = "file://" + aPath;
                SAL_INFO("vcl.fonts", "WASM font resolution: JS returned \""
                         << aPath << "\" for \""
                         << rFontSelData.maTargetName << "\"");

                if (registerFontFromVFS(aFileURL, rFontSelData.maTargetName))
                {
                    SAL_INFO("vcl.fonts", "WASM font resolution: registration "
                             "succeeded for \""
                             << rFontSelData.maTargetName << "\"");
                    // No positive cache needed -- font is now in PhysicalFontCollection,
                    // so FindFontFamily() in the caller finds it before we're called again.
                    return false;
                }

                SAL_WARN("vcl.fonts", "WASM font resolution: registration "
                         "failed for \"" << aFileURL << "\"");
                s_aWasmNegativeCache.insert(aCacheKey);
            }
            else
            {
                SAL_INFO("vcl.fonts", "WASM font resolution: JS returned "
                         "empty for \"" << rFontSelData.maTargetName << "\"");
                s_aWasmNegativeCache.insert(aCacheKey);
            }
        }
    }
#endif // EMSCRIPTEN

    OUString aDummy;
    vcl::font::FontSelectPattern aOut = GetFcSubstitute( rFontSelData, aDummy );

    const bool bHaveSubstitute = !aOut.maSearchName.isEmpty()
                                 && !uselessmatch( rFontSelData, aOut );

    if( aOut.maSearchName.isEmpty() )
        return false;

#if OSL_DEBUG_LEVEL >= 2
    std::ostringstream oss;
    oss << "FcPreMatchSubstitution \""
        << rFontSelData.maTargetName
        << "\" bipw="
        << rFontSelData.GetWeight()
        << rFontSelData.GetItalic()
        << rFontSelData.GetPitch()
        << rFontSelData.GetWidthType()
        << " -> ";
    if( !bHaveSubstitute )
        oss << "no substitute available.";
    else
        oss << "\""
            << aOut.maSearchName
            << "\" bipw="
            << aOut.GetWeight()
            << aOut.GetItalic()
            << aOut.GetPitch()
            << aOut.GetWidthType();
    SAL_INFO("vcl.fonts", oss.str());
#endif

    if( bHaveSubstitute )
    {
        rCachedFontMap.push_front(value_type(rFontSelData, aOut));
        // Fairly arbitrary limit in this case, but I recall measuring max 8
        // fonts as the typical max amount of fonts in medium sized documents, so make it
        // a fair chunk larger to accommodate weird documents./
        if (rCachedFontMap.size() > 256)
            rCachedFontMap.pop_back();
        rFontSelData = aOut;
    }

    return bHaveSubstitute;
}

bool FcGlyphFallbackSubstitution::FindFontSubstitute(vcl::font::FontSelectPattern& rFontSelData,
    LogicalFontInstance* /*pLogicalFont*/,
    OUString& rMissingCodes ) const
{
    // We don't actually want to talk to Fontconfig at all for symbol fonts
    if( rFontSelData.IsMicrosoftSymbolEncoded() )
        return false;
    // OpenSymbol is a unicode font, but it still deserves to be treated as a symbol font
    if ( IsOpenSymbol(rFontSelData.maSearchName) )
        return false;

    const vcl::font::FontSelectPattern aOut = GetFcSubstitute( rFontSelData, rMissingCodes );
    // TODO: cache the unicode + srcfont specific result
    // FC doing it would be preferable because it knows the invariables
    // e.g. FC knows the FC rule that all Arial gets replaced by LiberationSans
    // whereas we would have to check for every size or attribute
    if( aOut.maSearchName.isEmpty() )
        return false;

    const bool bHaveSubstitute = !uselessmatch( rFontSelData, aOut );

#if OSL_DEBUG_LEVEL >= 2
    std::ostringstream oss;
    oss << "FcGFSubstitution \""
        << rFontSelData.maTargetName
        << "\" bipw="
        << rFontSelData.GetWeight()
        << rFontSelData.GetItalic()
        << rFontSelData.GetPitch()
        << rFontSelData.GetWidthType()
        << " -> ";
    if( !bHaveSubstitute )
        oss << "no substitute available.";
    else
        oss << "\""
            << aOut.maSearchName
            << "\" bipw="
            << aOut.GetWeight()
            << aOut.GetItalic()
            << aOut.GetPitch()
            << aOut.GetWidthType();
    SAL_INFO("vcl.fonts", oss.str());
#endif

    if( bHaveSubstitute )
        rFontSelData = aOut;

    return bHaveSubstitute;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
