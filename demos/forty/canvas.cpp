/////////////////////////////////////////////////////////////////////////////
// Name:        canvas.cpp
// Purpose:     Forty Thieves patience game
// Author:      Chris Breeze
// Created:     21/07/97
// Copyright:   (c) 1993-1998 Chris Breeze
// Licence:     wxWindows licence
//---------------------------------------------------------------------------
// Last modified: 22nd July 1998 - ported to wxWidgets 2.0
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "forty.h"
#include "card.h"
#include "game.h"
#include "scorefil.h"
#include "playerdg.h"
#include "canvas.h"

wxBEGIN_EVENT_TABLE(FortyCanvas, wxScrolledWindow)
    EVT_MOUSE_EVENTS(FortyCanvas::OnMouseEvent)
wxEND_EVENT_TABLE()

FortyCanvas::FortyCanvas(wxWindow* parent, const wxPoint& pos, const wxSize& size) :
             wxScrolledWindow(parent, wxID_ANY, pos, size, 0),
             m_helpingHand(true),
             m_rightBtnUndo(true),
             m_playerDialog(0),
             m_leftBtnDown(false),
             m_backingStoreValid(false)
{
    SetScrollbars(0, 0, 0, 0);

#ifdef __WXGTK__
    m_font = wxTheFontList->FindOrCreateFont(wxFontInfo(12).Family(wxFONTFAMILY_ROMAN));
#else
    m_font = wxTheFontList->FindOrCreateFont(wxFontInfo(10).Family(wxFONTFAMILY_SWISS));
#endif
    SetBackgroundColour(FortyApp::BackgroundColour());

    m_handCursor = new wxCursor(wxCURSOR_HAND);
    m_arrowCursor = new wxCursor(wxCURSOR_ARROW);

    wxString name = wxTheApp->GetAppName();
    if ( name.empty() ) name = wxT("forty");
    m_scoreFile = new ScoreFile(name);
    m_game = new Game(0, 0, 0);
    m_game->Deal();
}


FortyCanvas::~FortyCanvas()
{
    UpdateScores();
    delete m_game;
    delete m_scoreFile;
    delete m_handCursor;
    delete m_arrowCursor;
}


/*
Write the current player's score back to the score file
*/
void FortyCanvas::UpdateScores()
{
    if (!m_player.empty() && m_scoreFile && m_game)
    {
        m_scoreFile->WritePlayersScore(
            m_player,
            m_game->GetNumWins(),
            m_game->GetNumGames(),
            m_game->GetScore()
        );
    }
}


/*
The game draws every change as it happens: clicking the pack deals a card and
draws it there and then, and dragging a card saves the pixels underneath it,
blits the card, then puts the saved pixels back.

Both halves of that need a drawing surface which keeps what was drawn on it and
can be read back again. A wxClientDC is not required to be either --
wxClientDC::CanBeUsedForDrawing() reports false under wxGTK on Wayland and on
the macOS and Qt ports, and under GTK4 there is no way to draw outside a
widget's snapshot or to read the screen back at all. There the game ran
correctly while nothing appeared: the board only caught up when an unrelated
repaint made OnDraw() redraw it from the game state.

So the game draws into a bitmap this canvas owns, which behaves the way it
expects everywhere, and OnDraw() puts that bitmap on the screen. The game code
itself is unchanged.
*/
bool FortyCanvas::PrepareBackingStore()
{
    const wxSize size = GetClientSize();
    if ( size.x <= 0 || size.y <= 0 )
        return false;

    if ( !m_backingStore.IsOk() || m_backingStoreSize != size )
    {
        if ( !m_backingStore.CreateWithLogicalSize(size, GetDPIScaleFactor()) )
            return false;

        m_backingStoreSize = size;
        m_backingStoreValid = false;
    }

    if ( !m_backingStoreValid )
    {
        wxMemoryDC dc(m_backingStore);

        // Game::Redraw() draws the piles and the score but not the baize
        // between them, which is the window's background colour when drawing
        // to the screen.
        dc.SetBackground(FortyApp::BackgroundBrush());
        dc.Clear();

        dc.SetFont(* m_font);
        m_game->Redraw(dc);

        m_backingStoreValid = true;

    }

    return true;
}


void FortyCanvas::InvalidateBackingStore()
{
    m_backingStoreValid = false;
    Refresh(false);
}


void FortyCanvas::OnDraw(wxDC& dc)
{
    if ( !PrepareBackingStore() )
        return;

    dc.DrawBitmap(m_backingStore, 0, 0);
#if 0
    // if player name not set (and selection dialog is not displayed)
    // then ask the player for their name
    if (m_player.empty() && !m_playerDialog)
    {
        m_playerDialog = new PlayerSelectionDialog(this, m_scoreFile);
        m_playerDialog->ShowModal();
        m_player = m_playerDialog->GetPlayersName();
        if ( !m_player.empty() )
        {
            // user entered a name - lookup their score
            int wins, games, score;
            m_scoreFile->ReadPlayersScore(m_player, wins, games, score);
            m_game->NewPlayer(wins, games, score);
            m_game->DisplayScore(dc);
            m_playerDialog->Destroy();
            m_playerDialog = 0;
            Refresh(false);
        }
        else
        {
            // user cancelled the dialog - exit the app
            ((wxFrame*)GetParent())->Close(true);
        }
    }
#endif
}

void FortyCanvas::ShowPlayerDialog()
{
    // if player name not set (and selection dialog is not displayed)
    // then ask the player for their name
    if (m_player.empty() && !m_playerDialog)
    {
        m_playerDialog = new PlayerSelectionDialog(this, m_scoreFile);
        m_playerDialog->ShowModal();
        m_player = m_playerDialog->GetPlayersName();
        if ( !m_player.empty() )
        {
            // user entered a name - lookup their score
            int wins, games, score;
            m_scoreFile->ReadPlayersScore(m_player, wins, games, score);
            m_game->NewPlayer(wins, games, score);

            m_playerDialog->Destroy();
            m_playerDialog = 0;

            // The score box has changed as a whole, so let the next paint draw
            // the board from the game again rather than patching it here.
            InvalidateBackingStore();
        }
        else
        {
            // user cancelled the dialog - exit the app
            ((wxFrame*)GetParent())->Close(true);
        }
    }
}

/*
Called when the main frame is closed
*/
bool FortyCanvas::OnCloseCanvas()
{
    if (m_game->InPlay() &&
        wxMessageBox(wxT("Are you sure you want to\nabandon the current game?"),
            wxT("Warning"), wxYES_NO | wxICON_QUESTION) == wxNO)
    {
        return false;
    }
    return true;
}

void FortyCanvas::OnMouseEvent(wxMouseEvent& event)
{
    int mouseX = (int)event.GetX();
    int mouseY = (int)event.GetY();

    if ( !PrepareBackingStore() )
        return;

    wxMemoryDC dc(m_backingStore);
    PrepareDC(dc);
    dc.SetFont(* m_font);

    // Most of the events arriving here are plain pointer motion, which only
    // changes the cursor. Repaint for the ones that actually draw something.
    bool drew = false;

    if (event.LeftDClick())
    {
        if (m_leftBtnDown)
        {
            m_leftBtnDown = false;
            ReleaseMouse();
            m_game->LButtonUp(dc, mouseX, mouseY);
        }
        m_game->LButtonDblClk(dc, mouseX, mouseY);
        drew = true;
    }
    else if (event.LeftDown())
    {
        if (!m_leftBtnDown)
        {
            m_leftBtnDown = true;
            CaptureMouse();
            m_game->LButtonDown(dc, mouseX, mouseY);
            drew = true;
        }
    }
    else if (event.LeftUp())
    {
        if (m_leftBtnDown)
        {
            m_leftBtnDown = false;
            ReleaseMouse();
            m_game->LButtonUp(dc, mouseX, mouseY);
            drew = true;
        }
    }
    else if (event.RightDown() && !event.LeftIsDown())
    {
        // only allow right button undo if m_rightBtnUndo is true
        if (m_rightBtnUndo)
        {
            if (event.ControlDown() || event.ShiftDown())
            {
                m_game->Redo(dc);
            }
            else
            {
                m_game->Undo(dc);
            }
            drew = true;
        }
    }
    else if (event.Dragging())
    {
        m_game->MouseMove(dc, mouseX, mouseY);
        drew = true;
    }

    if (drew)
    {
        // What the game drew went into the backing store, so ask for a paint
        // to put it on the screen. That paint runs after this handler has
        // returned and released the bitmap.
        Refresh(false);
    }

    if (!event.LeftIsDown())
    {
        SetCursorStyle(mouseX, mouseY);
    }
}

void FortyCanvas::SetCursorStyle(int x, int y)
{
    // Only set cursor to a hand if 'helping hand' is enabled and
    // the card under the cursor can go somewhere
    if (m_game->CanYouGo(x, y) && m_helpingHand)
    {
        SetCursor(* m_handCursor);
    }
    else
    {
        SetCursor(* m_arrowCursor);
    }

}

void FortyCanvas::NewGame()
{
    m_game->Deal();
    InvalidateBackingStore();
}

void FortyCanvas::Undo()
{
    if ( !PrepareBackingStore() )
        return;

    wxMemoryDC dc(m_backingStore);
    PrepareDC(dc);
    dc.SetFont(* m_font);
    m_game->Undo(dc);
    Refresh(false);
}

void FortyCanvas::Redo()
{
    if ( !PrepareBackingStore() )
        return;

    wxMemoryDC dc(m_backingStore);
    PrepareDC(dc);
    dc.SetFont(* m_font);
    m_game->Redo(dc);
    Refresh(false);
}

void FortyCanvas::LayoutGame()
{
       m_game->Layout();
       // Every pile has moved, and Game::Layout() has dropped the bitmaps the
       // game drags cards with, so the board has to be drawn again from
       // scratch.
       InvalidateBackingStore();
}
