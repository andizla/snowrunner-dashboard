// checkers_board.h: the checkers board's window, drawn with Dear ImGui over the NEXT GEN menu (BRIEF_checkers.md with
// the Reviewer's addendum). The rules, the opponent and the board's own state are checkers.h. It reads nothing from
// the game and writes nothing to it. The mod's overlay (gauge_overlay.cpp) and the offline preview draw it through
// this file.
//
// The controls come in as the kCk bits held on this frame, from the keys and the pad alike; the mouse is ImGui's.
// The cursor moves over the board and, one row under it, over the four buttons.

#pragma once
#include <math.h>
#include <stdio.h>
#include "imgui.h"
#include "checkers.h"

static volatile long g_ckBoardFrames = 0; // frames the board's code ran, for the test that off is off

// The texts of the board, each drawn as it stands here (the player text check reads this file).
static const char *const kCkButtonNames[kCkButtons] = { "New game", "One player", "Two players", "Close" };
static const char *const kCkChessLine = "Chess: when the donations come.";

// One frame of the window, in the middle of the screen and in front. held: the kCk bits held now. True: the player
// closes it (back with nothing picked, or Close).
static bool CkBoardDraw(CkBoard &b, unsigned held, float dt)
{
    g_ckBoardFrames++;
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowFocus();
    bool close = false;
    int clicked = -1, button = -1;
    if (ImGui::Begin("Checkers###nextgen_checkers", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar))
    {
        const float sq = floorf(ImGui::GetFontSize() * 2.6f), side = sq * 8.0f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##board", ImVec2(side, side));
        if (ImGui::IsItemClicked())
        {
            const int col = (int)((io.MousePos.x - at.x) / sq), row = 7 - (int)((io.MousePos.y - at.y) / sq);
            if (CkOn(row, col)) clicked = row * 8 + col;
        }
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImU32 light = IM_COL32(176, 172, 160, 255), dark = IM_COL32(74, 74, 80, 255), orange = IM_COL32(254, 158, 32, 255), white = IM_COL32(238, 234, 222, 255),
                    black = IM_COL32(22, 22, 24, 255), rim = IM_COL32(120, 120, 126, 255);
        bool to[64];
        const bool turn = b.game.result == kCkPlaying && (b.two || b.game.side == kCkWhite);
        if (turn) CkPickTargets(b.game, b.pick, to);
        else memset(to, 0, sizeof to);
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
            {
                const int s = row * 8 + col;
                const ImVec2 p0(at.x + col * sq, at.y + (7 - row) * sq), p1(p0.x + sq, p0.y + sq), mid(p0.x + sq * 0.5f, p0.y + sq * 0.5f);
                dl->AddRectFilled(p0, p1, CkDark(row, col) ? dark : light);
                for (int i = 0; i < b.pick.n; i++)
                    if (b.pick.path[i] == s) dl->AddRectFilled(p0, p1, IM_COL32(254, 158, 32, 90));
                const int piece = b.game.b[s];
                if (piece)
                {
                    const bool whiteSide = CkSide(piece) == kCkWhite;
                    dl->AddCircleFilled(mid, sq * 0.38f, whiteSide ? white : black, 32);
                    dl->AddCircle(mid, sq * 0.38f, rim, 32, sq * 0.04f);
                    if (CkKing(piece)) dl->AddCircle(mid, sq * 0.22f, orange, 24, sq * 0.07f);
                }
                if (to[s]) dl->AddCircleFilled(mid, sq * 0.10f, orange, 16);
            }
        if (b.cy >= 0)
        {
            const ImVec2 p0(at.x + b.cx * sq, at.y + (7 - b.cy) * sq);
            dl->AddRect(ImVec2(p0.x + 1.0f, p0.y + 1.0f), ImVec2(p0.x + sq - 1.0f, p0.y + sq - 1.0f), orange, 0.0f, 0, floorf(sq * 0.07f) + 1.0f);
        }
        ImGui::TextUnformatted(CkBoardStatus(b));
        const int over = b.cy < 0 ? b.cx * kCkButtons / 8 : -1;
        for (int i = 0; i < kCkButtons; i++)
        {
            if (i) ImGui::SameLine();
            // the players' choice in force is drawn as pressed
            const bool chosen = (i == kCkButtonOne && !b.two) || (i == kCkButtonTwo && b.two);
            if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(kCkButtonNames[i])) button = i;
            if (chosen) ImGui::PopStyleColor();
            if (i == over) dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), orange, 0.0f, 0, 2.0f);
        }
        ImGui::TextDisabled("%s", kCkChessLine);
    }
    ImGui::End();
    if (button >= 0) close = CkBoardButton(b, button);
    const unsigned pressed = CkControlsStep(b.controls, held, dt);
    return CkBoardStep(b, pressed, clicked, dt) || close;
}
