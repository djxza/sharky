#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define auto __auto_type

#include "../dev/stupid/utils.h" // provides ASSERT()

/* =========================
   Basic types
   ========================= */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef size_t usize;
typedef ptrdiff_t isize;

/* =========================
   Vector / Coordinates
   ========================= */

typedef struct v2 {
  u8 x;
  u8 y;
} v2;

/* =========================
   Chess basics
   ========================= */

#define WHITE 0
#define BLACK 1

typedef struct piece_t {
  u8 id;      // 1=pawn, 2=knight, 3=bishop, 4=rook, 5=queen, 6=king, 0=empty
  bool color; // WHITE or BLACK
} piece_t;

#define WIDTH 8
#define HEIGHT 8

typedef struct board_t {
  bool next_to_move; // WHITE or BLACK
  piece_t handle[WIDTH * HEIGHT];
} board_t;

/* =========================
   Indexing
   ========================= */

static inline u8 v2_idx(v2 p) { return (u8)(p.y * WIDTH + p.x); }

// Convert algebraic square (e.g. "a4") to 0‑based v2 index.
static inline u8 sq_idx(const char s[2]) {
  return v2_idx((v2){(u8)(s[0] - 'a'), (u8)(s[1] - '1')});
}

static inline bool v2_in_bounds(v2 p) { return p.x < WIDTH && p.y < HEIGHT; }

/* =========================
   FEN parsing
   ========================= */

#define PAWN 1
#define KNIGHT 2
#define BISHOP 3
#define ROOK 4
#define QUEEN 5
#define KING 6

#define PAWN_CH 'p'
#define KNIGHT_CH 'n'
#define BISHOP_CH 'b'
#define ROOK_CH 'r'
#define QUEEN_CH 'q'
#define KING_CH 'k'

#define CASE_CH_PC(p)                                                          \
  case p##_CH:                                                                 \
    return p

#define CASE_PC_CH(p)                                                          \
  case p:                                                                      \
    return p##_CH;

static u8 fen_piece_id(char c) {
  switch (c | 32) {
    CASE_CH_PC(PAWN);
    CASE_CH_PC(KNIGHT);
    CASE_CH_PC(BISHOP);
    CASE_CH_PC(ROOK);
    CASE_CH_PC(QUEEN);
    CASE_CH_PC(KING);

  default:
    return 0;
  }
}

void init_board_from_fen(board_t *board, const char *fen) {
  // Clear board
  for (usize i = 0; i < WIDTH * HEIGHT; i++) {
    board->handle[i].id = 0;
    board->handle[i].color = 0;
  }

  int x = 0;
  int y = 7; // start at rank 8 (FEN order)

  for (const char *p = fen; *p && *p != ' '; p++) {
    char c = *p;

    if (c == '/') {
      x = 0;
      y--;
      continue;
    }

    if (c >= '1' && c <= '8') {
      x += c - '0';
      continue;
    }

    u8 id = fen_piece_id(c);
    if (id) {
      board->handle[v2_idx((v2){(u8)x, (u8)y})] =
          (piece_t){.id = id, .color = (c >= 'a')}; // lowercase = black
      x++;
    }
  }
}

/* =========================
   Printing helpers
   ========================= */

static char id_ch(u8 id) {
  switch (id) {
    CASE_PC_CH(PAWN);
    CASE_PC_CH(KNIGHT);
    CASE_PC_CH(BISHOP);
    CASE_PC_CH(ROOK);
    CASE_PC_CH(QUEEN);
    CASE_PC_CH(KING);
  default:
    return ' ';
  }
}

char piece_to_ch(piece_t pc) {
  if (pc.id == 0)
    return '.';
  char c = id_ch(pc.id);
  return (pc.color == WHITE) ? toupper((unsigned char)c)
                             : tolower((unsigned char)c);
}

void print_pc(piece_t pc) { putchar(piece_to_ch(pc)); }

void print_bd(board_t bd) {
  for (int y = HEIGHT - 1; y >= 0; --y) {
    for (int x = 0; x < WIDTH; ++x) {
      print_pc(bd.handle[v2_idx((v2){(u8)x, (u8)y})]);
      putchar(' ');
    }
    putchar('\n');
  }
}

/* =========================
   Move structures & dynamic arrays
   ========================= */

typedef struct move_t {
  piece_t piece;
  v2 current_pos;
  v2 next_pos;
} move_t;

typedef struct move_list_t {
  move_t *handle;
  usize size;
} move_list_t;

typedef struct positions_list_t {
  v2 *handle;
  usize size;
} positions_list_t;

#define append(arena, el)                                                      \
  do {                                                                         \
    void *tmp = realloc((arena).handle,                                        \
                        ((arena).size + 1) * sizeof((arena).handle[0]));       \
    ASSERT(tmp, "Out of memory");                                              \
    (arena).handle = tmp;                                                      \
    (arena).handle[(arena).size++] = (el);                                     \
  } while (0)

/* =========================
   Move generation (pseudo‑legal, no check / pin detection)
   ========================= */

positions_list_t list_potentials_pawn(board_t bd, v2 piece_pos) {
  positions_list_t ret = {0};
  piece_t pc = bd.handle[v2_idx(piece_pos)];
  int dir =
      (pc.color == WHITE) ? 1 : -1; // white moves up (+y), black down (-y)

  // One square forward
  v2 one = {piece_pos.x, (u8)(piece_pos.y + dir)};
  if (v2_in_bounds(one) && bd.handle[v2_idx(one)].id == 0) {
    append(ret, one);
    // Two squares forward from starting rank
    if ((pc.color == WHITE && piece_pos.y == 1) ||
        (pc.color == BLACK && piece_pos.y == 6)) {
      v2 two = {piece_pos.x, (u8)(piece_pos.y + 2 * dir)};
      if (v2_in_bounds(two) && bd.handle[v2_idx(two)].id == 0) {
        append(ret, two);
      }
    }
  }

  // Captures left and right
  v2 cap_l = {(u8)(piece_pos.x - 1), (u8)(piece_pos.y + dir)};
  if (piece_pos.x > 0 && v2_in_bounds(cap_l)) {
    piece_t target = bd.handle[v2_idx(cap_l)];
    if (target.id != 0 && target.color != pc.color)
      append(ret, cap_l);
  }

  v2 cap_r = {(u8)(piece_pos.x + 1), (u8)(piece_pos.y + dir)};
  if (piece_pos.x + 1 < WIDTH && v2_in_bounds(cap_r)) {
    piece_t target = bd.handle[v2_idx(cap_r)];
    if (target.id != 0 && target.color != pc.color)
      append(ret, cap_r);
  }

  return ret;
}

positions_list_t list_potentials_knight(board_t bd, v2 piece_pos) {
  positions_list_t ret = {0};
  piece_t pc = bd.handle[v2_idx(piece_pos)];

  static const int dx[8] = {1, 2, 2, 1, -1, -2, -2, -1};
  static const int dy[8] = {2, 1, -1, -2, -2, -1, 1, 2};

  for (int i = 0; i < 8; ++i) {
    int nx = piece_pos.x + dx[i];
    int ny = piece_pos.y + dy[i];
    if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT)
      continue;

    v2 to = {(u8)nx, (u8)ny};
    piece_t target = bd.handle[v2_idx(to)];
    if (target.id == 0 || target.color != pc.color)
      append(ret, to);
  }
  return ret;
}

positions_list_t list_potentials_bishop(board_t bd, v2 piece_pos) {
  positions_list_t ret = {0};
  piece_t pc = bd.handle[v2_idx(piece_pos)];
  static const int dx[4] = {1, 1, -1, -1};
  static const int dy[4] = {1, -1, 1, -1};

  for (int i = 0; i < 4; ++i) {
    int nx = piece_pos.x + dx[i];
    int ny = piece_pos.y + dy[i];
    while (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
      v2 to = {(u8)nx, (u8)ny};
      piece_t target = bd.handle[v2_idx(to)];
      if (target.id == 0) {
        append(ret, to);
      } else {
        if (target.color != pc.color)
          append(ret, to);
        break;
      }
      nx += dx[i];
      ny += dy[i];
    }
  }
  return ret;
}

positions_list_t list_potentials_rook(board_t bd, v2 piece_pos) {
  positions_list_t ret = {0};
  piece_t pc = bd.handle[v2_idx(piece_pos)];
  static const int dx[4] = {1, -1, 0, 0};
  static const int dy[4] = {0, 0, 1, -1};

  for (int i = 0; i < 4; ++i) {
    int nx = piece_pos.x + dx[i];
    int ny = piece_pos.y + dy[i];
    while (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
      v2 to = {(u8)nx, (u8)ny};
      piece_t target = bd.handle[v2_idx(to)];
      if (target.id == 0) {
        append(ret, to);
      } else {
        if (target.color != pc.color)
          append(ret, to);
        break;
      }
      nx += dx[i];
      ny += dy[i];
    }
  }
  return ret;
}

positions_list_t list_potentials_queen(board_t bd, v2 piece_pos) {
  // Queen = bishop + rook
  positions_list_t ret = {0};
  piece_t pc = bd.handle[v2_idx(piece_pos)];
  static const int dx[8] = {1, 1, 1, 0, 0, -1, -1, -1};
  static const int dy[8] = {1, 0, -1, 1, -1, 1, 0, -1};

  for (int i = 0; i < 8; ++i) {
    int nx = piece_pos.x + dx[i];
    int ny = piece_pos.y + dy[i];
    while (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
      v2 to = {(u8)nx, (u8)ny};
      piece_t target = bd.handle[v2_idx(to)];
      if (target.id == 0) {
        append(ret, to);
      } else {
        if (target.color != pc.color)
          append(ret, to);
        break;
      }
      nx += dx[i];
      ny += dy[i];
    }
  }
  return ret;
}

positions_list_t list_potentials_king(board_t bd, v2 piece_pos) {
  positions_list_t ret = {0};
  piece_t pc = bd.handle[v2_idx(piece_pos)];
  static const int dx[8] = {1, 1, 1, 0, 0, -1, -1, -1};
  static const int dy[8] = {1, 0, -1, 1, -1, 1, 0, -1};

  for (int i = 0; i < 8; ++i) {
    int nx = piece_pos.x + dx[i];
    int ny = piece_pos.y + dy[i];
    if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
      v2 to = {(u8)nx, (u8)ny};
      piece_t target = bd.handle[v2_idx(to)];
      if (target.id == 0 || target.color != pc.color)
        append(ret, to);
    }
  }
  // Castling not implemented (special move)
  return ret;
}

positions_list_t list_potentials(board_t bd, v2 piece_pos) {
  positions_list_t null = {0};
  switch (bd.handle[v2_idx(piece_pos)].id) {
  case 1:
    return list_potentials_pawn(bd, piece_pos);
  case 2:
    return list_potentials_knight(bd, piece_pos);
  case 3:
    return list_potentials_bishop(bd, piece_pos);
  case 4:
    return list_potentials_rook(bd, piece_pos);
  case 5:
    return list_potentials_queen(bd, piece_pos);
  case 6:
    return list_potentials_king(bd, piece_pos);
  default:
    return null;
  }
}

move_list_t list_pseudo_legals(board_t bd) {
  move_list_t ret = {0};

  for (int y = HEIGHT - 1; y >= 0; --y) {
    for (int x = 0; x < WIDTH; ++x) {
      v2 pos = {(u8)x, (u8)y};
      piece_t pc = bd.handle[v2_idx(pos)];
      if (pc.id == 0 || pc.color != bd.next_to_move)
        continue;

      positions_list_t potentials = list_potentials(bd, pos);
      for (usize i = 0; i < potentials.size; ++i) {
        move_t m = {
            .piece = pc, .current_pos = pos, .next_pos = potentials.handle[i]};
        append(ret, m);
      }
      free(potentials.handle); // clean up temporary list
    }
  }
  return ret;
}

/* =========================
   Pretty printing of moves (fixed static‑buffer bug)
   ========================= */

// Write algebraic notation (e.g. "a1") into a caller‑provided 3‑char buffer.
void v2_to_algebraic_buf(v2 pos, char buf[3]) {
  buf[0] = 'a' + pos.x;
  buf[1] = '1' + pos.y;
  buf[2] = '\0';
}

void print_move(move_t m) {
  char from[3], to[3];
  v2_to_algebraic_buf(m.current_pos, from);
  v2_to_algebraic_buf(m.next_pos, to);
  printf("%c from %s to %s", piece_to_ch(m.piece), from, to);
}

void print_move_list(move_list_t list) {
  for (usize i = 0; i < list.size; ++i) {
    print_move(list.handle[i]);
    putchar('\n');
  }
}

/* =========================
   Custom printf for chess types
   ========================= */

void mprintf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  for (const char *p = fmt; *p; ++p) {
    if (*p != '%') {
      putchar(*p);
      continue;
    }

    ++p; // skip '%'
    if (*p == '\0')
      break;

    switch (*p) {
    case '%':
      putchar('%');
      break;

    case 'b': { // board_t*
      board_t *bd = va_arg(args, board_t *);
      print_bd(*bd);
      break;
    }

    case 'p': { // piece_t
      piece_t pc = va_arg(args, piece_t);
      print_pc(pc);
      break;
    }

    case 'v': { // v2 as (x,y)
      v2 pos = va_arg(args, v2);
      printf("(%d,%d)", pos.x, pos.y);
      break;
    }

    case 'a': { // v2 as algebraic
      v2 pos = va_arg(args, v2);
      char buf[3];
      v2_to_algebraic_buf(pos, buf);
      printf("%s", buf);
      break;
    }

    case 'm': { // move_t
      move_t m = va_arg(args, move_t);
      print_move(m);
      break;
    }

    case 'l': { // move_list_t*
      move_list_t *list = va_arg(args, move_list_t *);
      print_move_list(*list);
      break;
    }

    default:
      putchar('%');
      putchar(*p);
      break;
    }
  }

  va_end(args);
}

// fully legals here

// Test if a square is attacked by any piece of the given colour
bool is_attacked(board_t bd, v2 square, bool attacker_color) {
  // Knight attacks
  static const int knight_dx[8] = {1, 2, 2, 1, -1, -2, -2, -1};
  static const int knight_dy[8] = {2, 1, -1, -2, -2, -1, 1, 2};
  for (int i = 0; i < 8; ++i) {
    int x = square.x + knight_dx[i];
    int y = square.y + knight_dy[i];
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
      piece_t p = bd.handle[v2_idx((v2){(u8)x, (u8)y})];
      if (p.id == KNIGHT && p.color == attacker_color)
        return true;
    }
  }

  // King attacks (adjacent squares)
  static const int king_dx[8] = {1, 1, 1, 0, 0, -1, -1, -1};
  static const int king_dy[8] = {1, 0, -1, 1, -1, 1, 0, -1};
  for (int i = 0; i < 8; ++i) {
    int x = square.x + king_dx[i];
    int y = square.y + king_dy[i];
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
      piece_t p = bd.handle[v2_idx((v2){(u8)x, (u8)y})];
      if (p.id == KING && p.color == attacker_color)
        return true;
    }
  }

  // Pawn attacks (depends on attacker's colour)
  int pawn_dir = (attacker_color == WHITE)
                     ? 1
                     : -1; // white moves up, so attacks up-left/up-right
  // Attacker is white: pawn attacks from (x-1, y-1) and (x+1, y-1) relative to
  // square Attacker is black: pawn attacks from (x-1, y+1) and (x+1, y+1)
  // relative to square
  int pawn_att_y =
      square.y -
      pawn_dir; // because pawn stands one step behind the attacked square
  if (pawn_att_y >= 0 && pawn_att_y < HEIGHT) {
    if (square.x > 0) {
      v2 from = {(u8)(square.x - 1), (u8)pawn_att_y};
      piece_t p = bd.handle[v2_idx(from)];
      if (p.id == PAWN && p.color == attacker_color)
        return true;
    }
    if (square.x + 1 < WIDTH) {
      v2 from = {(u8)(square.x + 1), (u8)pawn_att_y};
      piece_t p = bd.handle[v2_idx(from)];
      if (p.id == PAWN && p.color == attacker_color)
        return true;
    }
  }

  // Sliding pieces: rook, bishop, queen
  // Directions:
  // 0=up,1=right,2=down,3=left,4=up-right,5=down-right,6=down-left,7=up-left
  static const int slide_dx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
  static const int slide_dy[8] = {1, 0, -1, 0, 1, -1, -1, 1};
  for (int d = 0; d < 8; ++d) {
    int x = square.x + slide_dx[d];
    int y = square.y + slide_dy[d];
    while (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
      piece_t p = bd.handle[v2_idx((v2){(u8)x, (u8)y})];
      if (p.id != 0) {
        // Found a piece
        if (p.color == attacker_color) {
          // Check if it's the right sliding type
          if (d < 4) { // orthogonal directions
            if (p.id == ROOK || p.id == QUEEN)
              return true;
          } else { // diagonal directions
            if (p.id == BISHOP || p.id == QUEEN)
              return true;
          }
        }
        break; // block further in this direction
      }
      x += slide_dx[d];
      y += slide_dy[d];
    }
  }

  return false;
}

// Find king of a specific colour
u8 find_king_of_color(board_t bd, bool color) {
  for (int i = 0; i < WIDTH * HEIGHT; ++i)
    if (bd.handle[i].id == KING && bd.handle[i].color == color)
      return i;
  return UINT8_MAX;
}

board_t apply_move(board_t bd, move_t mv) {
  ASSERT(v2_in_bounds(mv.next_pos), "Out of bounds");

  bd.handle[v2_idx(mv.next_pos)] = bd.handle[v2_idx(mv.current_pos)];
  bd.handle[v2_idx(mv.current_pos)].id = 0;
  bd.handle[v2_idx(mv.current_pos)].color = 0;

  printf("\n");
  printf("\n");
  mprintf("%b", &bd);
  printf("\n");
  printf("\n");

  return bd;
}

// Check whether a move is legal (does not leave own king in check)
bool is_legal_move(board_t bd, move_t mv) {
  board_t after = apply_move(bd, mv); // make the move on a copy
  u8 king_idx;
  if (mv.piece.id == KING) {
    king_idx = v2_idx(mv.next_pos); // king moved – new position
  } else {
    king_idx = find_king_of_color(after, mv.piece.color);
  }

  ASSERT(king_idx != UINT8_MAX, "King missing after move");
  v2 king_pos = {king_idx % WIDTH, king_idx / WIDTH};
  bool opponent = !mv.piece.color;
  return !is_attacked(after, king_pos, opponent);
}

// Generate legal moves from pseudo‑legal list
move_list_t list_legals(board_t bd, move_list_t *pseudo_legals) {
  move_list_t ret = {0};
  for (usize i = 0; i < pseudo_legals->size; ++i) {
    if (is_legal_move(bd, pseudo_legals->handle[i])) {
      append(ret, pseudo_legals->handle[i]);
    }
  }
  return ret;
}

/* =========================
   Main (example)
   ========================= */

int main(void) {
  board_t bd = {0};
  init_board_from_fen(&bd, "8/8/8/2k5/3b4/8/1P6/K7");
  bd.next_to_move = WHITE;

  printf("Initial board:\n");
  print_bd(bd);
  printf("\n");
  move_list_t pseudo_legals = list_pseudo_legals(bd);

  printf("Pseudo‑legal moves for White:\n");
  mprintf("%l", &pseudo_legals);

  move_list_t legals = list_legals(bd, &pseudo_legals);

  printf("Legal moves for White:\n");
  mprintf("%l", &legals);

  free(pseudo_legals.handle);
  free(legals.handle);
  return 0;
}
