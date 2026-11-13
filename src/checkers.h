// checkers.h: Russian draughts, the rules, a small opponent and the little state the board's window needs. Plain C++
// with no game memory, no drawing and no clock in it, so the tests run offline (BRIEF_checkers.md with the Reviewer's
// addendum).
//
// The rules:
//   8 by 8, the dark square at each player's lower left (a1), twelve men a side on the dark squares of the three
//   nearest rows. White moves first.
//   A man moves one square forward along a diagonal and captures forward and backward.
//   A capture is compulsory. Any capture may be picked, not the longest. A capture goes on to its end.
//   The men taken stay on the board until the move ends and none is jumped twice: a taken man blocks the way.
//   A man that reaches the far row becomes a king at once, in the middle of a capture too, and goes on as a king.
//   A king flies along the diagonal. A king that takes lands on a square from which it can take again, where there
//   is one, else on any free square behind the man.
//   A side with no move loses. A draw after kCkDrawQuiet moves in a row by kings with no capture (15 a side), or when
//   a position comes for the third time with the same side to move.
//
// Squares are numbered row * 8 + column, row 0 white's home row, column 0 the a file.

#pragma once
#include <stdint.h>
#include <string.h>

enum { kCkEmpty, kCkWhiteMan, kCkWhiteKing, kCkBlackMan, kCkBlackKing };
enum { kCkWhite, kCkBlack };
enum { kCkPlaying, kCkWhiteWins, kCkBlackWins, kCkDraw };

static const int kCkMaxPath = 13;     // a move's squares: where it starts and a landing for each of at most twelve men taken
static const int kCkMaxMoves = 96;    // the moves kept of one position; more than that are cut (CkMoves::cut), the kept ones are legal
static const int kCkDrawQuiet = 30;   // moves in a row by kings with no capture that make a draw
// The positions the opponent looks at for one move, at the most. By the clock a position costs about a third of a
// microsecond and a microsecond at the worst met, and the most met in 200 self games is 609 (the test prints both): so
// 1500 keeps a move under two milliseconds on the drawing thread at the worst, and is never reached in those games.
static const int kCkSearchMax = 1500;
static const int kCkLost = 100000;

inline int CkSide(int piece) { return piece == kCkWhiteMan || piece == kCkWhiteKing ? kCkWhite : piece == kCkBlackMan || piece == kCkBlackKing ? kCkBlack : -1; }
inline bool CkKing(int piece) { return piece == kCkWhiteKing || piece == kCkBlackKing; }
inline bool CkDark(int row, int col) { return ((row + col) & 1) == 0; }
inline bool CkOn(int row, int col) { return row >= 0 && row < 8 && col >= 0 && col < 8; }
// The row a man of the side is crowned on.
inline int CkFarRow(int side) { return side == kCkWhite ? 7 : 0; }

// A move: its squares from the start to the end (n of them, each landing of a capture between), the men it takes and
// whether the man was crowned on the way or at the end.
struct CkMove
{
    int8_t n, path[kCkMaxPath];
    int8_t ntaken, taken[12];
    bool crowned;
};
struct CkMoves
{
    int n;
    bool capture; // they are captures, and nothing else is legal
    bool cut;     // there were more than kCkMaxMoves
    CkMove m[kCkMaxMoves];
};
// A game: the board, the side to move, how it stands, the kings' quiet moves in a row and the positions since the last
// move that cannot be taken back (a man's move or a capture), for the third time.
struct CkGame
{
    uint8_t b[64];
    int side, result, quiet, plies, seen;
    uint64_t hist[kCkDrawQuiet + 2];
};

static const int kCkDr[4] = { 1, 1, -1, -1 }, kCkDc[4] = { 1, -1, 1, -1 };

// ---- the moves of a position ----

struct CkGen
{
    uint8_t b[64]; // the board with the moving piece lifted off its square
    int side;
    uint64_t taken; // the men taken so far in this move: still on the board, in the way
    CkMove cur;
    CkMoves *out;
};

inline void CkEmit(CkGen &x)
{
    if (x.out->n < kCkMaxMoves) x.out->m[x.out->n++] = x.cur;
    else x.out->cut = true;
}

// Whether the piece on sq can take a man from there. The same conditions as CkJumps finds its captures by.
inline bool CkCanJump(const CkGen &x, int sq, bool king)
{
    const int r = sq >> 3, c = sq & 7;
    for (int d = 0; d < 4; d++)
    {
        int mr = r + kCkDr[d], mc = c + kCkDc[d];
        if (king)
            while (CkOn(mr, mc) && x.b[mr * 8 + mc] == kCkEmpty) mr += kCkDr[d], mc += kCkDc[d];
        if (!CkOn(mr, mc)) continue;
        const int mid = mr * 8 + mc;
        if (CkSide(x.b[mid]) != 1 - x.side || ((x.taken >> mid) & 1)) continue;
        const int lr = mr + kCkDr[d], lc = mc + kCkDc[d];
        if (CkOn(lr, lc) && x.b[lr * 8 + lc] == kCkEmpty) return true;
    }
    return false;
}

// Every capture of the piece on sq, each played to its end and put out. False: it can take nothing from there.
inline bool CkJumps(CkGen &x, int sq, bool king)
{
    bool any = false;
    const int r = sq >> 3, c = sq & 7;
    for (int d = 0; d < 4; d++)
    {
        int mr = r + kCkDr[d], mc = c + kCkDc[d];
        if (king)
            while (CkOn(mr, mc) && x.b[mr * 8 + mc] == kCkEmpty) mr += kCkDr[d], mc += kCkDc[d];
        if (!CkOn(mr, mc)) continue;
        const int mid = mr * 8 + mc;
        if (CkSide(x.b[mid]) != 1 - x.side || ((x.taken >> mid) & 1)) continue;
        int lr = mr + kCkDr[d], lc = mc + kCkDc[d];
        if (!CkOn(lr, lc) || x.b[lr * 8 + lc] != kCkEmpty) continue;
        if (x.cur.ntaken >= 12 || x.cur.n >= kCkMaxPath) continue; // never met: a side has twelve men
        any = true;
        x.taken |= 1ull << mid;
        x.cur.taken[x.cur.ntaken++] = (int8_t)mid;
        // where it lands: a man on the one square behind, a king on any free square behind. Of a king's squares only
        // those it can take again from, where there is one
        int lands[7], count = 0;
        bool on[7], kingAt[7], some = false;
        for (; CkOn(lr, lc) && x.b[lr * 8 + lc] == kCkEmpty && count < 7; lr += kCkDr[d], lc += kCkDc[d])
        {
            lands[count] = lr * 8 + lc;
            kingAt[count] = king || lr == CkFarRow(x.side);
            on[count] = CkCanJump(x, lands[count], kingAt[count]);
            some = some || on[count];
            count++;
            if (!king) break;
        }
        for (int i = 0; i < count; i++)
        {
            if (king && some && !on[i]) continue;
            const bool was = x.cur.crowned;
            x.cur.path[x.cur.n++] = (int8_t)lands[i];
            if (kingAt[i] && !king) x.cur.crowned = true;
            if (on[i]) CkJumps(x, lands[i], kingAt[i]);
            else CkEmit(x);
            x.cur.n--;
            x.cur.crowned = was;
        }
        x.cur.ntaken--;
        x.taken &= ~(1ull << mid);
    }
    return any;
}

// The legal moves of the side on the board: the captures where there is one, else the quiet moves.
inline void CkLegalBoard(const uint8_t b[64], int side, CkMoves &out)
{
    out.n = 0;
    out.capture = out.cut = false;
    CkGen x;
    memcpy(x.b, b, 64);
    x.side = side;
    x.taken = 0;
    x.out = &out;
    for (int sq = 0; sq < 64; sq++)
    {
        if (CkSide(b[sq]) != side) continue;
        x.cur = CkMove{};
        x.cur.n = 1;
        x.cur.path[0] = (int8_t)sq;
        x.b[sq] = kCkEmpty;
        CkJumps(x, sq, CkKing(b[sq]));
        x.b[sq] = b[sq];
    }
    if (out.n > 0)
    {
        out.capture = true;
        return;
    }
    for (int sq = 0; sq < 64; sq++)
    {
        if (CkSide(b[sq]) != side) continue;
        const bool king = CkKing(b[sq]);
        const int r = sq >> 3, c = sq & 7;
        for (int d = 0; d < 4; d++)
        {
            if (!king && kCkDr[d] != (side == kCkWhite ? 1 : -1)) continue;
            for (int tr = r + kCkDr[d], tc = c + kCkDc[d]; CkOn(tr, tc) && b[tr * 8 + tc] == kCkEmpty; tr += kCkDr[d], tc += kCkDc[d])
            {
                CkMove m = {};
                m.n = 2;
                m.path[0] = (int8_t)sq;
                m.path[1] = (int8_t)(tr * 8 + tc);
                m.crowned = !king && tr == CkFarRow(side);
                if (out.n < kCkMaxMoves) out.m[out.n++] = m;
                else out.cut = true;
                if (!king) break;
            }
        }
    }
}

// The move made on a board: the piece to its last square, the men taken off, the crown.
inline void CkBoardApply(uint8_t b[64], const CkMove &m)
{
    const int from = m.path[0], to = m.path[m.n - 1];
    uint8_t piece = b[from];
    b[from] = kCkEmpty;
    for (int i = 0; i < m.ntaken; i++) b[m.taken[i]] = kCkEmpty;
    if (m.crowned) piece = CkSide(piece) == kCkWhite ? (uint8_t)kCkWhiteKing : (uint8_t)kCkBlackKing;
    b[to] = piece;
}

// ---- a game ----

inline uint64_t CkHash(const CkGame &g)
{
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 64; i++) h = (h ^ g.b[i]) * 1099511628211ull;
    return (h ^ (uint64_t)(g.side + 7)) * 1099511628211ull;
}

inline void CkLegal(const CkGame &g, CkMoves &out) { CkLegalBoard(g.b, g.side, out); }

// How the game stands for the side to move: no move loses, then the two draws.
inline void CkJudge(CkGame &g)
{
    CkMoves mv;
    CkLegal(g, mv);
    int same = 0;
    const uint64_t h = CkHash(g);
    for (int i = 0; i < g.seen; i++) same += g.hist[i] == h;
    g.result = mv.n == 0 ? (g.side == kCkWhite ? kCkBlackWins : kCkWhiteWins) : g.quiet >= kCkDrawQuiet || same >= 3 ? kCkDraw : kCkPlaying;
}

// An empty board with white to move, for a position put up by hand. CkRemember and CkJudge after the pieces are on.
inline void CkClear(CkGame &g)
{
    g = CkGame{};
    g.side = kCkWhite;
}
inline void CkRemember(CkGame &g)
{
    if (g.seen < kCkDrawQuiet + 2) g.hist[g.seen++] = CkHash(g);
}

inline void CkNew(CkGame &g)
{
    CkClear(g);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (CkDark(r, c)) g.b[r * 8 + c] = r < 3 ? (uint8_t)kCkWhiteMan : r > 4 ? (uint8_t)kCkBlackMan : (uint8_t)kCkEmpty;
    CkRemember(g);
    CkJudge(g);
}

// A legal move made: the board, the side, the count of the kings' quiet moves, the positions seen, and how it stands.
inline void CkMake(CkGame &g, const CkMove &m)
{
    const bool back = CkKing(g.b[m.path[0]]) && m.ntaken == 0; // a king's quiet move is the one kind that can be taken back
    CkBoardApply(g.b, m);
    g.side = 1 - g.side;
    g.plies++;
    if (back) g.quiet++;
    else g.quiet = 0, g.seen = 0;
    CkRemember(g);
    CkJudge(g);
}

// Whether m is one of the position's legal moves, square for square.
inline bool CkIsLegal(const CkGame &g, const CkMove &m)
{
    CkMoves mv;
    CkLegal(g, mv);
    for (int i = 0; i < mv.n; i++)
        if (mv.m[i].n == m.n && mv.m[i].ntaken == m.ntaken && mv.m[i].crowned == m.crowned && !memcmp(mv.m[i].path, m.path, (size_t)m.n) &&
            !memcmp(mv.m[i].taken, m.taken, (size_t)m.ntaken))
            return true;
    return false;
}

// ---- the opponent ----
// It looks two moves ahead, its own and the answer, with the captures after them played out, and counts the men: a man
// 100, a king 300. Among the best it picks by a seed of its own. It looks at kCkSearchMax positions at the most.

struct CkSearch { int positions, max; };

inline int CkMaterial(const uint8_t b[64], int side)
{
    int v = 0;
    for (int i = 0; i < 64; i++)
        if (b[i]) v += (CkKing(b[i]) ? 300 : 100) * (CkSide(b[i]) == side ? 1 : -1);
    return v;
}

// The position for the side to move, depth moves deep and then for as long as there is a capture (eight deep at most).
inline int CkLook(const uint8_t b[64], int side, CkSearch &s, int depth, int quiet = 8)
{
    if (s.positions >= s.max) return CkMaterial(b, side);
    s.positions++;
    CkMoves mv;
    CkLegalBoard(b, side, mv);
    if (mv.n == 0) return -kCkLost;
    if (depth <= 0 && (!mv.capture || quiet <= 0)) return CkMaterial(b, side);
    int best = -2 * kCkLost;
    for (int i = 0; i < mv.n; i++)
    {
        uint8_t nb[64];
        memcpy(nb, b, 64);
        CkBoardApply(nb, mv.m[i]);
        const int v = -CkLook(nb, 1 - side, s, depth - 1, depth <= 0 ? quiet - 1 : quiet);
        if (v > best) best = v;
    }
    return best;
}

// Its own generator (xorshift): never the game's, never the C library's.
inline uint32_t CkRandom(uint32_t &seed)
{
    if (!seed) seed = 0x9E3779B9u;
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

// The opponent's move for the side to move. False: there is none. positions: how many it looked at.
inline bool CkOpponent(const CkGame &g, uint32_t &seed, CkMove &out, int &positions)
{
    positions = 0;
    CkMoves mv;
    CkLegal(g, mv);
    if (mv.n == 0) return false;
    CkSearch s = { 0, kCkSearchMax };
    int best = -4 * kCkLost, pick[kCkMaxMoves], picks = 0;
    for (int i = 0; i < mv.n; i++)
    {
        uint8_t nb[64];
        memcpy(nb, g.b, 64);
        CkBoardApply(nb, mv.m[i]);
        const int v = -CkLook(nb, 1 - g.side, s, 1);
        if (v > best) best = v, picks = 0;
        if (v == best) pick[picks++] = i;
    }
    positions = s.positions;
    out = mv.m[pick[CkRandom(seed) % (uint32_t)picks]];
    return true;
}

// ---- the player's pick, square by square ----
// The squares picked so far: the piece's own, then each landing. A square that leads on along a legal move is added.
// When the squares are a whole move it is played.

struct CkPick { int n; int8_t path[kCkMaxPath]; };
enum { kCkPickNo, kCkPickOn, kCkPickPlayed };

inline bool CkPickFits(const CkMove &m, const CkPick &p) { return m.n >= p.n && !memcmp(m.path, p.path, (size_t)p.n); }

// The square picked. kCkPickPlayed: the move is whole and made, in played. kCkPickOn: the pick grew, or began anew on
// another piece that can move. kCkPickNo: the square does nothing.
inline int CkPickSquare(CkGame &g, CkPick &p, int sq, CkMove &played)
{
    if (g.result != kCkPlaying || sq < 0 || sq >= 64) return kCkPickNo;
    CkMoves mv;
    CkLegal(g, mv);
    CkPick q = p;
    if (q.n < kCkMaxPath)
    {
        q.path[q.n++] = (int8_t)sq;
        bool any = false;
        for (int i = 0; i < mv.n; i++)
        {
            if (!CkPickFits(mv.m[i], q)) continue;
            any = true;
            if (mv.m[i].n == q.n && q.n > 1)
            {
                played = mv.m[i];
                CkMake(g, played);
                p.n = 0;
                return kCkPickPlayed;
            }
        }
        if (any)
        {
            p = q;
            return kCkPickOn;
        }
    }
    for (int i = 0; i < mv.n; i++)
        if (mv.m[i].path[0] == sq)
        {
            p.n = 1;
            p.path[0] = (int8_t)sq;
            return kCkPickOn;
        }
    return kCkPickNo;
}

// The squares the pick can go on to (with nothing picked: the pieces that can move).
inline void CkPickTargets(const CkGame &g, const CkPick &p, bool to[64])
{
    memset(to, 0, 64 * sizeof(bool));
    if (g.result != kCkPlaying) return;
    CkMoves mv;
    CkLegal(g, mv);
    for (int i = 0; i < mv.n; i++)
        if (CkPickFits(mv.m[i], p) && mv.m[i].n > p.n) to[mv.m[i].path[p.n]] = true;
}

// ---- the board's window: the hidden opening, the controls and what opening and closing ask of the menu ----

// Five clicks within three seconds.
struct CkClicks { int n; double at[5]; };
inline bool CkClick(CkClicks &c, double now)
{
    if (c.n == 5)
    {
        memmove(c.at, c.at + 1, 4 * sizeof(double));
        c.n = 4;
    }
    c.at[c.n++] = now;
    if (c.n < 5 || now - c.at[0] > 3.0 || now < c.at[0]) return false;
    c.n = 0;
    return true;
}

// The controls, from the pad and from the keys alike. A direction held repeats after 0.4 s, every 0.15 s. Pick and back
// count once a press.
enum { kCkLeft = 1, kCkRight = 2, kCkUp = 4, kCkDown = 8, kCkPickKey = 16, kCkBackKey = 32 };
struct CkControls { unsigned held; float since[4]; bool primed; };
inline unsigned CkControlsStep(CkControls &c, unsigned now, float dt)
{
    // what is held when the board opens is no press: it opened on it
    if (!c.primed)
    {
        c.primed = true;
        c.held = now;
        for (int i = 0; i < 4; i++) c.since[i] = 0.0f;
        return 0;
    }
    unsigned out = now & ~c.held & (kCkPickKey | kCkBackKey);
    for (int i = 0; i < 4; i++)
    {
        const unsigned bit = 1u << i;
        if (!(now & bit))
        {
            c.since[i] = 0.0f;
            continue;
        }
        if (!(c.held & bit))
        {
            out |= bit;
            c.since[i] = 0.0f;
            continue;
        }
        c.since[i] += dt;
        if (c.since[i] >= 0.4f)
        {
            out |= bit;
            c.since[i] -= 0.15f;
        }
    }
    c.held = now;
    return out;
}

// The board shows only with the NEXT GEN menu open: the menu's take of the keys is the board's (no take of its own).
// Opened with the menu closed, the board opens the menu and closes it again when it goes. Opened in an open menu, it
// leaves the menu as it found it. So after every way of closing, the keys are held or free as before the opening.
struct CkWindow { bool open, openedMenu; };
enum { kCkMenuLeave, kCkMenuShow, kCkMenuClose };

inline int CkWindowOpen(CkWindow &w, bool menuOpen)
{
    if (w.open) return kCkMenuLeave;
    w.open = true;
    w.openedMenu = !menuOpen;
    return kCkMenuShow; // the menu on the mod's own tab, opened where it was closed
}
// Closed by the player: back, the binding again, the board's Close.
inline int CkWindowClose(CkWindow &w)
{
    if (!w.open) return kCkMenuLeave;
    const bool mine = w.openedMenu;
    w.open = w.openedMenu = false;
    return mine ? kCkMenuClose : kCkMenuLeave;
}
// Every frame: the menu gone (its own key closed it) takes the board with it, and the keys are free already. The
// game's window not in front or another truck closes the board as the player would.
inline int CkWindowFrame(CkWindow &w, bool menuOpen, bool front, bool sameTruck)
{
    if (!w.open) return kCkMenuLeave;
    if (!menuOpen)
    {
        w.open = w.openedMenu = false;
        return kCkMenuLeave;
    }
    return front && sameTruck ? kCkMenuLeave : CkWindowClose(w);
}

// The mod's own bindings (the gears, the locks) rest while the board is open: the board is open only with the menu
// open (CkWindowFrame closes it the frame the menu is gone), and under the menu they rest already.

// ---- the board: a game kept while the mod runs, the cursor, the buttons under it and the opponent's wait. No drawing ----

struct CkBoard
{
    bool started; // a game is set up; it is kept while the game runs, through a close of the window
    bool two;     // two players on the same controls, turn by turn; else white against the mod
    CkGame game;
    CkPick pick;
    int cx, cy;   // the cursor: a square, or cy -1 for the row of buttons
    uint32_t seed;
    float think;  // seconds the opponent has waited since its turn began
    CkControls controls;
    int worst;    // the most positions the opponent looked at for a move
};

static const float kCkThink = 0.5f; // seconds the opponent waits before it moves, so the player sees the move
enum { kCkButtonNew, kCkButtonOne, kCkButtonTwo, kCkButtonClose, kCkButtons };

inline void CkBoardNew(CkBoard &b)
{
    CkNew(b.game);
    b.pick.n = 0;
    b.think = 0.0f;
    b.started = true;
}

// The window was opened: the controls held on it are no press, and the first opening sets a game up with the seed.
inline void CkBoardOpened(CkBoard &b, uint32_t seed)
{
    b.controls = CkControls{};
    if (b.started) return;
    b.seed = seed ? seed : 1u;
    b.cx = 2, b.cy = 2;
    CkBoardNew(b);
}

// A button of the row under the board. True: the window is to close.
inline bool CkBoardButton(CkBoard &b, int button)
{
    if (button == kCkButtonNew) CkBoardNew(b);
    else if (button == kCkButtonOne) b.two = false, b.think = 0.0f;
    else if (button == kCkButtonTwo) b.two = true;
    return button == kCkButtonClose;
}

// The game's part of a frame, with no drawing: the controls' presses (kCk bits, from CkControlsStep), a square clicked
// with the mouse (below 0: none) and the opponent's wait. True: the player closes the window.
inline bool CkBoardStep(CkBoard &b, unsigned pressed, int clicked, float dt)
{
    bool close = false;
    const bool turn = b.game.result == kCkPlaying && (b.two || b.game.side == kCkWhite); // a player is to move
    CkMove played;
    if (clicked >= 0 && clicked < 64)
    {
        b.cx = clicked & 7, b.cy = clicked >> 3;
        if (turn) CkPickSquare(b.game, b.pick, clicked, played);
    }
    if (pressed & kCkLeft) b.cx = b.cx > 0 ? b.cx - 1 : 0;
    if (pressed & kCkRight) b.cx = b.cx < 7 ? b.cx + 1 : 7;
    if (pressed & kCkUp) b.cy = b.cy < 7 ? b.cy + 1 : 7;
    if (pressed & kCkDown) b.cy = b.cy > -1 ? b.cy - 1 : -1;
    if (pressed & kCkPickKey)
    {
        if (b.cy < 0) close = CkBoardButton(b, b.cx * kCkButtons / 8);
        else if (turn) CkPickSquare(b.game, b.pick, b.cy * 8 + b.cx, played);
    }
    if (pressed & kCkBackKey)
    {
        // back takes the pick back a square, and with nothing picked closes the board
        if (b.pick.n > 0) b.pick.n--;
        else close = true;
    }
    // the opponent plays black: it waits kCkThink, then moves
    if (!b.two && b.game.result == kCkPlaying && b.game.side == kCkBlack)
    {
        b.think += dt;
        if (b.think >= kCkThink)
        {
            CkMove m;
            int positions = 0;
            if (CkOpponent(b.game, b.seed, m, positions)) CkMake(b.game, m);
            if (positions > b.worst) b.worst = positions;
            b.think = 0.0f;
            b.pick.n = 0;
        }
    }
    else b.think = 0.0f;
    return close;
}

inline const char *CkBoardStatus(const CkBoard &b)
{
    switch (b.game.result)
    {
    case kCkWhiteWins: return "White wins";
    case kCkBlackWins: return "Black wins";
    case kCkDraw: return "Draw";
    }
    return b.game.side == kCkWhite ? "White to move" : "Black to move";
}

// The controls held, from a pad: its d-pad or left stick, A and B. up, down, left, right: the pad's bits for each (the
// d-pad's and the stick's together); pick and back: A and B.
inline unsigned CkHeldOf(bool left, bool right, bool up, bool down, bool pick, bool back)
{
    return (left ? kCkLeft : 0u) | (right ? kCkRight : 0u) | (up ? kCkUp : 0u) | (down ? kCkDown : 0u) | (pick ? kCkPickKey : 0u) | (back ? kCkBackKey : 0u);
}
