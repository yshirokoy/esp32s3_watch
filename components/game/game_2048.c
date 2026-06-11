#include "game_2048.h"
#include <stdlib.h>
#include <string.h>

// Safe font selection (match watch_ui.h config)
#if LV_FONT_MONTSERRAT_24
  #define GAME_MID_FONT  &lv_font_montserrat_24
#elif LV_FONT_MONTSERRAT_18
  #define GAME_MID_FONT  &lv_font_montserrat_18
#else
  #define GAME_MID_FONT  LV_FONT_DEFAULT
#endif

#if LV_FONT_MONTSERRAT_18
  #define GAME_BODY_FONT &lv_font_montserrat_18
#else
  #define GAME_BODY_FONT LV_FONT_DEFAULT
#endif

#define GRID_SIZE  4
#define CELL_SIZE  50
#define CELL_GAP   5
#define GRID_X     12
#define GRID_Y     70

static int  s_board[GRID_SIZE][GRID_SIZE];
static int  s_score;
static bool s_active;
static bool s_won;

static lv_obj_t *s_tiles[GRID_SIZE][GRID_SIZE];
static lv_obj_t *s_score_label;
static lv_obj_t *s_start_btn;
static lv_obj_t *s_back_btn;
static lv_obj_t *s_overlay;
static lv_obj_t *s_game_cont;
static void (*s_lock_cb)(bool lock) = NULL;
static bool s_paused = false;

static int  s_touch_start_x = -1;
static int  s_touch_start_y = -1;
static bool s_touch_down = false;

// --- Tile colors ---
static uint32_t tile_bg(int val)
{
    switch (val) {
        case 2:    return 0xEEE4DA;
        case 4:    return 0xEDE0C8;
        case 8:    return 0xF2B179;
        case 16:   return 0xF59563;
        case 32:   return 0xF67C5F;
        case 64:   return 0xF65E3B;
        case 128:  return 0xEDCF72;
        case 256:  return 0xEDCC61;
        case 512:  return 0xEDC850;
        case 1024: return 0xEDC53F;
        case 2048: return 0xEDC22E;
        default:   return 0xCDC1B4;
    }
}

static uint32_t tile_fg(int val)
{
    return (val <= 4) ? 0x776E65 : 0xFFFFFF;
}

// --- Game logic ---
static void add_random_tile(void)
{
    int empty[GRID_SIZE * GRID_SIZE][2];
    int cnt = 0;
    for (int r = 0; r < GRID_SIZE; r++)
        for (int c = 0; c < GRID_SIZE; c++)
            if (s_board[r][c] == 0) {
                empty[cnt][0] = r;
                empty[cnt][1] = c;
                cnt++;
            }
    if (cnt == 0) return;
    int idx = rand() % cnt;
    s_board[empty[idx][0]][empty[idx][1]] = (rand() % 10 < 9) ? 2 : 4;
}

static bool slide_row(int *row)
{
    bool moved = false;
    int pos = 0;
    for (int i = 0; i < GRID_SIZE; i++) {
        if (row[i] != 0) {
            if (i != pos) {
                row[pos] = row[i];
                row[i] = 0;
                moved = true;
            }
            // Merge with previous
            if (pos > 0 && row[pos] == row[pos - 1]) {
                row[pos - 1] *= 2;
                s_score += row[pos - 1];
                row[pos] = 0;
                moved = true;
            } else {
                pos++;
            }
        }
    }
    return moved;
}

static bool move_board(int dr, int dc)
{
    bool moved = false;
    // Horizontal move
    if (dr == 0) {
        for (int r = 0; r < GRID_SIZE; r++) {
            int row[GRID_SIZE];
            if (dc > 0) { // right — reverse
                for (int c = 0; c < GRID_SIZE; c++)
                    row[GRID_SIZE - 1 - c] = s_board[r][c];
            } else { // left
                for (int c = 0; c < GRID_SIZE; c++)
                    row[c] = s_board[r][c];
            }
            if (slide_row(row)) moved = true;
            if (dc > 0) {
                for (int c = 0; c < GRID_SIZE; c++)
                    s_board[r][c] = row[GRID_SIZE - 1 - c];
            } else {
                for (int c = 0; c < GRID_SIZE; c++)
                    s_board[r][c] = row[c];
            }
        }
    }
    // Vertical move
    if (dc == 0) {
        for (int c = 0; c < GRID_SIZE; c++) {
            int col[GRID_SIZE];
            if (dr > 0) { // down — reverse
                for (int r = 0; r < GRID_SIZE; r++)
                    col[GRID_SIZE - 1 - r] = s_board[r][c];
            } else { // up
                for (int r = 0; r < GRID_SIZE; r++)
                    col[r] = s_board[r][c];
            }
            if (slide_row(col)) moved = true;
            if (dr > 0) {
                for (int r = 0; r < GRID_SIZE; r++)
                    s_board[r][c] = col[GRID_SIZE - 1 - r];
            } else {
                for (int r = 0; r < GRID_SIZE; r++)
                    s_board[r][c] = col[r];
            }
        }
    }
    return moved;
}

static bool can_move(void)
{
    for (int r = 0; r < GRID_SIZE; r++)
        for (int c = 0; c < GRID_SIZE; c++) {
            if (s_board[r][c] == 0) return true;
            if (c < GRID_SIZE - 1 && s_board[r][c] == s_board[r][c + 1]) return true;
            if (r < GRID_SIZE - 1 && s_board[r][c] == s_board[r + 1][c]) return true;
        }
    return false;
}

// --- Rendering ---
static void refresh_board(void)
{
    for (int r = 0; r < GRID_SIZE; r++)
        for (int c = 0; c < GRID_SIZE; c++) {
            int val = s_board[r][c];
            lv_obj_t *tile = s_tiles[r][c];
            if (!tile) continue;
            if (val == 0) {
                lv_obj_add_flag(tile, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(tile, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_color(tile, lv_color_hex(tile_bg(val)), 0);
                lv_obj_t *label = lv_obj_get_child(tile, 0);
                if (label) {
                    lv_label_set_text_fmt(label, "%d", val);
                    lv_obj_set_style_text_color(label, lv_color_hex(tile_fg(val)), 0);
                }
            }
        }
    if (s_score_label) {
        lv_label_set_text_fmt(s_score_label, "Score: %d", s_score);
    }
}

// --- Back button: pause game, unlock tileview ---
static void on_back_click(lv_event_t *e)
{
    (void)e;
    s_paused = true;
    if (s_back_btn) lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_start_btn) {
        lv_label_set_text(lv_obj_get_child(s_start_btn, 0), "Continue");
        lv_obj_clear_flag(s_start_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_lock_cb) s_lock_cb(false);
}

// --- Swipe detection ---
static void on_game_touch(lv_event_t *e)
{
    if (!s_active || s_paused) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    if (code == LV_EVENT_PRESSED) {
        s_touch_start_x = pt.x;
        s_touch_start_y = pt.y;
        s_touch_down = true;
        // Lock tileview while finger is down (when game active)
        if (s_active && s_lock_cb) s_lock_cb(true);
    } else if (code == LV_EVENT_RELEASED && s_touch_down) {
        int dx = pt.x - s_touch_start_x;
        int dy = pt.y - s_touch_start_y;
        int adx = abs(dx);
        int ady = abs(dy);
        s_touch_down = false;
        // Unlock tileview as soon as finger lifts
        if (s_lock_cb) s_lock_cb(false);

        // Minimum swipe distance
        if (adx < 20 && ady < 20) return;

        bool moved = false;
        if (adx > ady) {
            moved = move_board(0, (dx > 0) ? 1 : -1);
        } else {
            moved = move_board((dy > 0) ? 1 : -1, 0);
        }

        if (moved) {
            add_random_tile();
            refresh_board();
            if (!s_won) {
                for (int r = 0; r < GRID_SIZE; r++)
                    for (int c = 0; c < GRID_SIZE; c++)
                        if (s_board[r][c] == 2048) s_won = true;
            }
            if (!can_move()) {
                s_active = false;
                if (s_score_label)
                    lv_label_set_text_fmt(s_score_label, "Game Over!  %d", s_score);
                if (s_start_btn) {
                    lv_label_set_text(lv_obj_get_child(s_start_btn, 0), "Start");
                    lv_obj_clear_flag(s_start_btn, LV_OBJ_FLAG_HIDDEN);
                }
                if (s_back_btn)
                    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

// --- Start button wrapper ---
static void on_start_click(lv_event_t *e)
{
    (void)e;
    game_2048_start();
}

// --- Public API ---
void game_2048_set_lock_cb(void (*cb)(bool lock))
{
    s_lock_cb = cb;
}

void game_2048_start(void)
{
    if (s_paused) {
        // Resume existing game
        s_paused = false;
        if (s_start_btn)
            lv_obj_add_flag(s_start_btn, LV_OBJ_FLAG_HIDDEN);
        if (s_back_btn)
            lv_obj_clear_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // New game
    memset(s_board, 0, sizeof(s_board));
    s_score = 0;
    s_active = true;
    s_won = false;
    s_paused = false;
    if (s_start_btn)
        lv_obj_add_flag(s_start_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_back_btn)
        lv_obj_clear_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
    add_random_tile();
    add_random_tile();
    refresh_board();
    if (s_score_label)
        lv_label_set_text_fmt(s_score_label, "Score: 0");
}

bool game_2048_is_active(void)
{
    return s_active;
}

lv_obj_t *game_2048_create(lv_obj_t *parent)
{
    s_game_cont = lv_obj_create(parent);
    lv_obj_set_size(s_game_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_game_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_game_cont, 0, 0);
    lv_obj_set_style_pad_all(s_game_cont, 0, 0);
    lv_obj_set_scroll_dir(s_game_cont, LV_DIR_NONE);  // no scroll — game handles swipes
    lv_obj_clear_flag(s_game_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(s_game_cont);
    lv_label_set_text(title, "2048");
    lv_obj_set_style_text_font(title, GAME_MID_FONT, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x776E65), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Score
    s_score_label = lv_label_create(s_game_cont);
    lv_label_set_text(s_score_label, "Score: 0");
    lv_obj_set_style_text_font(s_score_label, GAME_BODY_FONT, 0);
    lv_obj_set_style_text_color(s_score_label, lv_color_hex(0x776E65), 0);
    lv_obj_align(s_score_label, LV_ALIGN_TOP_MID, 0, 40);

    // Board background
    lv_obj_t *board_bg = lv_obj_create(s_game_cont);
    int board_px = GRID_SIZE * CELL_SIZE + (GRID_SIZE + 1) * CELL_GAP;
    lv_obj_set_size(board_bg, board_px, board_px);
    lv_obj_set_pos(board_bg, GRID_X - CELL_GAP, GRID_Y - CELL_GAP);
    lv_obj_set_style_bg_color(board_bg, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_bg_opa(board_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(board_bg, 0, 0);
    lv_obj_set_style_radius(board_bg, 6, 0);

    // Create tiles (cells)
    for (int r = 0; r < GRID_SIZE; r++)
        for (int c = 0; c < GRID_SIZE; c++) {
            lv_obj_t *cell = lv_obj_create(s_game_cont);
            int cell_x = GRID_X + c * (CELL_SIZE + CELL_GAP);
            int cell_y = GRID_Y + r * (CELL_SIZE + CELL_GAP);
            lv_obj_set_size(cell, CELL_SIZE, CELL_SIZE);
            lv_obj_set_pos(cell, cell_x, cell_y);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0xCDC1B4), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *label = lv_label_create(cell);
            lv_label_set_text(label, "");
            lv_obj_set_style_text_font(label, GAME_MID_FONT, 0);
            lv_obj_center(label);

            s_tiles[r][c] = cell;
        }

    // Touch overlay for swipe detection
    s_overlay = lv_obj_create(s_game_cont);
    lv_obj_set_size(s_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_add_event_cb(s_overlay, on_game_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_overlay, on_game_touch, LV_EVENT_RELEASED, NULL);

    // Start button (centered)
    s_start_btn = lv_btn_create(s_game_cont);
    lv_obj_set_size(s_start_btn, 120, 50);
    lv_obj_align(s_start_btn, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(s_start_btn, lv_color_hex(0x8D6E53), 0);
    lv_obj_set_style_bg_opa(s_start_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_start_btn, 0, 0);
    lv_obj_set_style_radius(s_start_btn, 10, 0);
    lv_obj_add_event_cb(s_start_btn, on_start_click, LV_EVENT_CLICKED, NULL);

    // Back button (top-right, visible only during game)
    s_back_btn = lv_btn_create(s_game_cont);
    lv_obj_set_size(s_back_btn, 40, 30);
    lv_obj_align(s_back_btn, LV_ALIGN_TOP_RIGHT, -4, 6);
    lv_obj_set_style_bg_color(s_back_btn, lv_color_hex(0x8D6E53), 0);
    lv_obj_set_style_bg_opa(s_back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_back_btn, 0, 0);
    lv_obj_set_style_radius(s_back_btn, 6, 0);
    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_back_btn, on_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(s_back_btn);
    lv_label_set_text(back_lbl, "X");
    lv_obj_set_style_text_font(back_lbl, GAME_BODY_FONT, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_lbl);

    lv_obj_t *btn_lbl = lv_label_create(s_start_btn);
    lv_label_set_text(btn_lbl, "Start");
    lv_obj_set_style_text_font(btn_lbl, GAME_BODY_FONT, 0);
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_lbl);

    return s_game_cont;
}
