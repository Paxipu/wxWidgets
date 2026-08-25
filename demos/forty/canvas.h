/////////////////////////////////////////////////////////////////////////////
// Name:        canvas.h
// Purpose:     Forty Thieves patience game
// Author:      Chris Breeze
// Created:     21/07/97
// Copyright:   (c) 1993-1998 Chris Breeze
// Licence:     wxWindows licence
//---------------------------------------------------------------------------
// Last modified: 22nd July 1998 - ported to wxWidgets 2.0
/////////////////////////////////////////////////////////////////////////////
#ifndef _CANVAS_H_
#define _CANVAS_H_

class Card;
class Game;
class ScoreFile;
class PlayerSelectionDialog;

class FortyCanvas: public wxScrolledWindow
{
public:
    FortyCanvas(wxWindow* parent, const wxPoint& pos, const wxSize& size);
    virtual ~FortyCanvas();

    virtual void OnDraw(wxDC& dc) override;
    bool OnCloseCanvas();
    void OnMouseEvent(wxMouseEvent& event);
    void SetCursorStyle(int x, int y);

    void NewGame();
    void Undo();
    void Redo();

    ScoreFile* GetScoreFile() const { return m_scoreFile; }
    void UpdateScores();
    void EnableHelpingHand(bool enable) { m_helpingHand = enable; }
    void EnableRightButtonUndo(bool enable) { m_rightBtnUndo = enable; }
    void LayoutGame();
    void ShowPlayerDialog();

    wxDECLARE_EVENT_TABLE();

private:
    // Make sure the backing store exists, matches the client size and holds a
    // current picture of the board; returns false if it cannot be created, in
    // which case there is nothing to draw on and the caller does nothing. See
    // the comment on it in canvas.cpp for why the game does not draw straight
    // to the screen.
    bool PrepareBackingStore();

    // Drop the drawn board, so that the next paint redraws it from the game.
    // Use whenever something changes the board as a whole.
    void InvalidateBackingStore();

    wxFont* m_font;
    Game* m_game;
    ScoreFile* m_scoreFile;
    wxCursor* m_arrowCursor;
    wxCursor* m_handCursor;
    bool m_helpingHand;
    bool m_rightBtnUndo;
    wxString m_player;
    PlayerSelectionDialog* m_playerDialog;
    bool m_leftBtnDown;

    // The board as drawn so far, and the client size it was drawn for. That
    // size is remembered rather than asked of the bitmap, so that rounding in
    // the conversion to physical pixels cannot make the store look stale on
    // every single paint.
    wxBitmap m_backingStore;
    wxSize m_backingStoreSize;
    bool m_backingStoreValid;
};

#endif
