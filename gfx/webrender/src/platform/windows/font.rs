/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{FontInstancePlatformOptions, FontKey, FontRenderMode};
use api::{ColorU, GlyphDimensions, GlyphKey, SubpixelDirection};
use dwrote;
use gamma_lut::{ColorLut, GammaLut};
use glyph_rasterizer::{FontInstance, GlyphFormat, RasterizedGlyph};
use internal_types::FastHashMap;
use std::sync::Arc;

lazy_static! {
    static ref DEFAULT_FONT_DESCRIPTOR: dwrote::FontDescriptor = dwrote::FontDescriptor {
        family_name: "Arial".to_owned(),
        weight: dwrote::FontWeight::Regular,
        stretch: dwrote::FontStretch::Normal,
        style: dwrote::FontStyle::Normal,
    };
}

pub struct FontContext {
    fonts: FastHashMap<FontKey, dwrote::FontFace>,
    gamma_lut: GammaLut,
    gdi_gamma_lut: GammaLut,
}

// DirectWrite is safe to use on multiple threads and non-shareable resources are
// all hidden inside their font context.
unsafe impl Send for FontContext {}

fn dwrite_texture_type(render_mode: FontRenderMode) -> dwrote::DWRITE_TEXTURE_TYPE {
    match render_mode {
        FontRenderMode::Mono | FontRenderMode::Bitmap => dwrote::DWRITE_TEXTURE_ALIASED_1x1,
        FontRenderMode::Alpha | FontRenderMode::Subpixel => dwrote::DWRITE_TEXTURE_CLEARTYPE_3x1,
    }
}

fn dwrite_measure_mode(
    render_mode: FontRenderMode,
    options: Option<FontInstancePlatformOptions>,
) -> dwrote::DWRITE_MEASURING_MODE {
    if let Some(FontInstancePlatformOptions {
        force_gdi_rendering: true,
        ..
    }) = options
    {
        return dwrote::DWRITE_MEASURING_MODE_GDI_CLASSIC;
    }

    match render_mode {
        FontRenderMode::Mono | FontRenderMode::Bitmap => dwrote::DWRITE_MEASURING_MODE_GDI_NATURAL,
        FontRenderMode::Alpha | FontRenderMode::Subpixel => dwrote::DWRITE_MEASURING_MODE_NATURAL,
    }
}

fn dwrite_render_mode(
    font_face: &dwrote::FontFace,
    render_mode: FontRenderMode,
    em_size: f32,
    measure_mode: dwrote::DWRITE_MEASURING_MODE,
    options: Option<FontInstancePlatformOptions>,
) -> dwrote::DWRITE_RENDERING_MODE {
    if let Some(FontInstancePlatformOptions {
        force_gdi_rendering: true,
        ..
    }) = options
    {
        return dwrote::DWRITE_RENDERING_MODE_GDI_CLASSIC;
    }

    let dwrite_render_mode = match render_mode {
        FontRenderMode::Mono | FontRenderMode::Bitmap => dwrote::DWRITE_RENDERING_MODE_ALIASED,
        FontRenderMode::Alpha | FontRenderMode::Subpixel => {
            font_face.get_recommended_rendering_mode_default_params(em_size, 1.0, measure_mode)
        }
    };

    if dwrite_render_mode == dwrote::DWRITE_RENDERING_MODE_OUTLINE {
        // Outline mode is not supported
        return dwrote::DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;
    }

    dwrite_render_mode
}

impl FontContext {
    pub fn new() -> FontContext {
        // These are the default values we use in Gecko.
        // We use a gamma value of 2.3 for gdi fonts
        // TODO: Fetch this data from Gecko itself.
        let contrast = 1.0;
        let gamma = 1.8;
        let gdi_gamma = 2.3;
        FontContext {
            fonts: FastHashMap::default(),
            gamma_lut: GammaLut::new(contrast, gamma, gamma),
            gdi_gamma_lut: GammaLut::new(contrast, gdi_gamma, gdi_gamma),
        }
    }

    pub fn has_font(&self, font_key: &FontKey) -> bool {
        self.fonts.contains_key(font_key)
    }

    pub fn add_raw_font(&mut self, font_key: &FontKey, data: Arc<Vec<u8>>, index: u32) {
        if self.fonts.contains_key(font_key) {
            return;
        }

        if let Some(font_file) = dwrote::FontFile::new_from_data(&**data) {
            let face = font_file.create_face(index, dwrote::DWRITE_FONT_SIMULATIONS_NONE);
            self.fonts.insert((*font_key).clone(), face);
        } else {
            // XXX add_raw_font needs to have a way to return an error
            debug!("DWrite WR failed to load font from data, using Arial instead");
            self.add_native_font(font_key, DEFAULT_FONT_DESCRIPTOR.clone());
        }
    }

    pub fn add_native_font(&mut self, font_key: &FontKey, font_handle: dwrote::FontDescriptor) {
        if self.fonts.contains_key(font_key) {
            return;
        }

        let system_fc = dwrote::FontCollection::system();
        let font = system_fc.get_font_from_descriptor(&font_handle).unwrap();
        let face = font.create_font_face();
        self.fonts.insert((*font_key).clone(), face);
    }

    pub fn delete_font(&mut self, font_key: &FontKey) {
        self.fonts.remove(font_key);
    }

    // Assumes RGB format from dwrite, which is 3 bytes per pixel as dwrite
    // doesn't output an alpha value via GlyphRunAnalysis::CreateAlphaTexture
    #[allow(dead_code)]
    fn print_glyph_data(&self, data: &[u8], width: usize, height: usize) {
        // Rust doesn't have step_by support on stable :(
        for i in 0 .. height {
            let current_height = i * width * 3;

            for pixel in data[current_height .. current_height + (width * 3)].chunks(3) {
                let r = pixel[0];
                let g = pixel[1];
                let b = pixel[2];
                print!("({}, {}, {}) ", r, g, b,);
            }
            println!("");
        }
    }

    fn create_glyph_analysis(
        &self,
        font: &FontInstance,
        key: &GlyphKey,
    ) -> dwrote::GlyphRunAnalysis {
        let face = self.fonts.get(&font.font_key).unwrap();
        let glyph = key.index as u16;
        let advance = 0.0f32;
        let offset = dwrote::GlyphOffset {
            advanceOffset: 0.0,
            ascenderOffset: 0.0,
        };

        let glyph_run = dwrote::DWRITE_GLYPH_RUN {
            fontFace: unsafe { face.as_ptr() },
            fontEmSize: font.size.to_f32_px(), // size in DIPs (1/96", same as CSS pixels)
            glyphCount: 1,
            glyphIndices: &glyph,
            glyphAdvances: &advance,
            glyphOffsets: &offset,
            isSideways: 0,
            bidiLevel: 0,
        };

        let dwrite_measure_mode = dwrite_measure_mode(font.render_mode, font.platform_options);
        let dwrite_render_mode = dwrite_render_mode(
            face,
            font.render_mode,
            font.size.to_f32_px(),
            dwrite_measure_mode,
            font.platform_options,
        );

        let (x_offset, y_offset) = font.get_subpx_offset(key);
        let transform = Some(dwrote::DWRITE_MATRIX {
            m11: 1.0,
            m12: 0.0,
            m21: 0.0,
            m22: 1.0,
            dx: x_offset as f32,
            dy: y_offset as f32,
        });

        dwrote::GlyphRunAnalysis::create(
            &glyph_run,
            1.0,
            transform,
            dwrite_render_mode,
            dwrite_measure_mode,
            0.0,
            0.0,
        )
    }

    pub fn get_glyph_index(&mut self, font_key: FontKey, ch: char) -> Option<u32> {
        let face = self.fonts.get(&font_key).unwrap();
        let indices = face.get_glyph_indices(&[ch as u32]);
        indices.first().map(|idx| *idx as u32)
    }

    // TODO: Pipe GlyphOptions into glyph_dimensions too
    pub fn get_glyph_dimensions(
        &self,
        font: &FontInstance,
        key: &GlyphKey,
    ) -> Option<GlyphDimensions> {
        // Probably have to default to something else here.
        let render_mode = FontRenderMode::Subpixel;
        let analysis = self.create_glyph_analysis(font, key);

        let texture_type = dwrite_texture_type(render_mode);

        let bounds = analysis.get_alpha_texture_bounds(texture_type);

        let width = (bounds.right - bounds.left) as u32;
        let height = (bounds.bottom - bounds.top) as u32;

        // Alpha texture bounds can sometimes return an empty rect
        // Such as for spaces
        if width == 0 || height == 0 {
            return None;
        }

        let face = self.fonts.get(&font.font_key).unwrap();
        face.get_design_glyph_metrics(&[key.index as u16], false)
            .first()
            .map(|metrics| {
                let em_size = font.size.to_f32_px() / 16.;
                let design_units_per_pixel = face.metrics().designUnitsPerEm as f32 / 16. as f32;
                let scaled_design_units_to_pixels = em_size / design_units_per_pixel;
                let advance = metrics.advanceWidth as f32 * scaled_design_units_to_pixels;

                GlyphDimensions {
                    left: bounds.left,
                    top: -bounds.top,
                    width,
                    height,
                    advance: advance,
                }
            })
    }

    // DWrite ClearType gives us values in RGB, but WR expects BGRA.
    fn convert_to_bgra(&self, pixels: &[u8], render_mode: FontRenderMode) -> Vec<u8> {
        match render_mode {
            FontRenderMode::Bitmap => {
                unreachable!("TODO: bitmap fonts");
            }
            FontRenderMode::Mono => {
                let mut bgra_pixels: Vec<u8> = vec![0; pixels.len() * 4];
                for i in 0 .. pixels.len() {
                    let alpha = pixels[i];
                    bgra_pixels[i * 4 + 0] = alpha;
                    bgra_pixels[i * 4 + 1] = alpha;
                    bgra_pixels[i * 4 + 2] = alpha;
                    bgra_pixels[i * 4 + 3] = alpha;
                }
                bgra_pixels
            }
            FontRenderMode::Alpha => {
                let length = pixels.len() / 3;
                let mut bgra_pixels: Vec<u8> = vec![0; length * 4];
                for i in 0 .. length {
                    // Only take the G channel, as its closest to D2D
                    let alpha = pixels[i * 3 + 1] as u8;
                    bgra_pixels[i * 4 + 0] = alpha;
                    bgra_pixels[i * 4 + 1] = alpha;
                    bgra_pixels[i * 4 + 2] = alpha;
                    bgra_pixels[i * 4 + 3] = alpha;
                }
                bgra_pixels
            }
            FontRenderMode::Subpixel => {
                let length = pixels.len() / 3;
                let mut bgra_pixels: Vec<u8> = vec![0; length * 4];
                for i in 0 .. length {
                    bgra_pixels[i * 4 + 0] = pixels[i * 3 + 2];
                    bgra_pixels[i * 4 + 1] = pixels[i * 3 + 1];
                    bgra_pixels[i * 4 + 2] = pixels[i * 3 + 0];
                    bgra_pixels[i * 4 + 3] = 0xff;
                }
                bgra_pixels
            }
        }
    }

    pub fn is_bitmap_font(&mut self, _font: &FontInstance) -> bool {
        // TODO(gw): Support bitmap fonts in DWrite.
        false
    }

    pub fn prepare_font(font: &mut FontInstance) {
        match font.render_mode {
            FontRenderMode::Mono | FontRenderMode::Bitmap => {
                // In mono/bitmap modes the color of the font is irrelevant.
                font.color = ColorU::new(255, 255, 255, 255);
                // Subpixel positioning is disabled in mono and bitmap modes.
                font.subpx_dir = SubpixelDirection::None;
            }
            FontRenderMode::Alpha => {
                font.color = font.color.luminance_color().quantize();
            }
            FontRenderMode::Subpixel => {
                font.color = font.color.quantize();
            }
        }
    }

    pub fn rasterize_glyph(
        &mut self,
        font: &FontInstance,
        key: &GlyphKey,
    ) -> Option<RasterizedGlyph> {
        let analysis = self.create_glyph_analysis(font, key);
        let texture_type = dwrite_texture_type(font.render_mode);

        let bounds = analysis.get_alpha_texture_bounds(texture_type);
        let width = (bounds.right - bounds.left) as u32;
        let height = (bounds.bottom - bounds.top) as u32;

        // Alpha texture bounds can sometimes return an empty rect
        // Such as for spaces
        if width == 0 || height == 0 {
            return None;
        }

        let pixels = analysis.create_alpha_texture(texture_type, bounds);
        let mut bgra_pixels = self.convert_to_bgra(&pixels, font.render_mode);

        match font.render_mode {
            FontRenderMode::Mono | FontRenderMode::Bitmap => {}
            FontRenderMode::Alpha | FontRenderMode::Subpixel => {
                let lut_correction = match font.platform_options {
                    Some(option) => if option.force_gdi_rendering {
                        &self.gdi_gamma_lut
                    } else {
                        &self.gamma_lut
                    },
                    None => &self.gamma_lut,
                };

                lut_correction.preblend(&mut bgra_pixels, font.color);
            }
        }

        Some(RasterizedGlyph {
            left: bounds.left as f32,
            top: -bounds.top as f32,
            width,
            height,
            scale: 1.0,
            format: GlyphFormat::from(font.render_mode),
            bytes: bgra_pixels,
        })
    }
}
