#pragma once

#include "document_model.hpp"
#include "tile_cache.hpp"
#include "tile_manager.hpp"
#include "viewport.hpp"

#include "lvgl.h"

namespace reader {

lv_obj_t *page_canvas_create(lv_obj_t *parent);
void page_canvas_bind(lv_obj_t *canvas, Viewport *viewport, docs::DocumentModel *model,
                      TileCache *cache, TileManager *manager, uint32_t doc_hash, int tile_size);
void page_canvas_set_dim_overlay(lv_obj_t *canvas, float alpha);
void page_canvas_set_dragging(lv_obj_t *canvas, bool dragging);
void page_canvas_invalidate(lv_obj_t *canvas);
void page_canvas_refresh(lv_obj_t *canvas);

}  // namespace reader
