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
#include <set>
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

// Set of font family names already attempted via JS resolution.
// Prevents infinite recursion and serves as a negative cache.
static std::set<OUString> s_aTriedFonts;

// Async font resolution via JSPI. The resolver callback is stored on
// globalThis.__resolveSystemFont by the Electron app (libreoffice-wasm.ts)
// because Emscripten's Module initialization drops custom properties.
// Returns a VFS path as a C string (caller must free), or 0 on failure.
EM_ASYNC_JS(char*, em_resolveFontFromHost, (const char* pFamilyName), {
    var familyName = UTF8ToString(pFamilyName);
    // Emscripten drops custom Module properties during init.
    // The app stores the resolver on globalThis.__resolveSystemFont.
    var resolver = (typeof globalThis !== 'undefined' && globalThis.__resolveSystemFont)
                || Module.resolveSystemFont;

    if (!resolver) {
        console.warn('em_resolveFontFromHost: no resolver available');
        return 0;
    }

    try {
        var fontData = await resolver(familyName);
        if (!fontData || fontData.byteLength === 0) {
            return 0;
        }
        var safeName = familyName.replace(/[^a-zA-Z0-9_-]/g, '_');
        var path = '/tmp/fonts/' + safeName + '.ttf';
        try { FS.mkdirTree('/tmp/fonts'); } catch(e) {}
        FS.writeFile(path, new Uint8Array(fontData));
        console.warn('em_resolveFontFromHost: wrote font to', path);
        return stringToNewUTF8(path);
    } catch(e) {
        console.warn('WASM font resolution failed for: ' + familyName, e);
        return 0;
    }
});

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
    // In WASM builds, try to resolve missing fonts from the host JavaScript
    // environment BEFORE fontconfig substitution. Fontconfig always finds a
    // substitute (e.g. "Segoe UI" → "Liberation Sans"), so gating on
    // !bHaveSubstitute never works. Instead, check whether the exact font
    // family exists in the PhysicalFontCollection.
    if (s_pFontCollection && !s_pFontCollection->FindFontFamily(rFontSelData.maTargetName))
    {
        if (s_aTriedFonts.find(rFontSelData.maTargetName) == s_aTriedFonts.end())
        {
            s_aTriedFonts.insert(rFontSelData.maTargetName);

            OString aUtf8 = OUStringToOString(rFontSelData.maTargetName,
                                               RTL_TEXTENCODING_UTF8);
            SAL_INFO("vcl.fonts", "WASM font resolution: requesting \""
                     << rFontSelData.maTargetName << "\" from host");

            char* pPath = em_resolveFontFromHost(aUtf8.getStr());
            if (pPath)
            {
                OUString aPath = OStringToOUString(
                    OString(pPath), RTL_TEXTENCODING_UTF8);
                free(pPath);

                OUString aFileURL = "file://" + aPath;
                SAL_INFO("vcl.fonts", "WASM font resolution: received path \""
                         << aPath << "\", registering");

                if (registerFontFromVFS(aFileURL, rFontSelData.maTargetName))
                {
                    SAL_INFO("vcl.fonts", "WASM font resolution: registered, "
                             "caller will find it in collection");
                    // Font now in the PhysicalFontCollection. Return false
                    // (no substitute) so the caller's
                    // ImplFindFontFamilyBySearchName() finds the newly
                    // registered font directly by its normalized name.
                    return false;
                }

                SAL_WARN("vcl.fonts", "WASM font resolution: registerFontFromVFS "
                         "failed for \"" << aFileURL << "\"");
            }
            else
            {
                SAL_INFO("vcl.fonts", "WASM font resolution: host returned no font for \""
                         << rFontSelData.maTargetName << "\"");
            }
        }
        else
        {
            SAL_INFO("vcl.fonts", "WASM font already tried, skipping: \""
                     << rFontSelData.maTargetName << "\"");
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
