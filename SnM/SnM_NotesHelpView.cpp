/******************************************************************************
/ SnM_NotesHelpView.cpp
/
/ Copyright (c) 2010-2012 Jeffos
/ http://www.standingwaterstudios.com/reaper
/
/ Permission is hereby granted, free of charge, to any person obtaining a copy
/ of this software and associated documentation files (the "Software"), to deal
/ in the Software without restriction, including without limitation the rights to
/ use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
/ of the Software, and to permit persons to whom the Software is furnished to
/ do so, subject to the following conditions:
/ 
/ The above copyright notice and this permission notice shall be included in all
/ copies or substantial portions of the Software.
/ 
/ THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
/ EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
/ OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
/ NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
/ HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
/ WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/ FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
/ OTHER DEALINGS IN THE SOFTWARE.
/
******************************************************************************/

//JFB
// - atm, undo on each key stroke (!) but it fixes:
//   * SaveExtensionConfig() that is not called when there is no proj mods but
//     some notes have been entered..
//   * missing updates on project switches
// - clicking the empty area of the tcp does not remove focus (important for refresh)
// - undo does not restore caret pos
// TODO?
// - take changed => title not updated
// - drag-drop text?
// - use action_help_t? (not finished according to Cockos)

#include "stdafx.h"
#include "SnM.h"
#include "../reaper/localize.h"


#define MAX_HELP_LENGTH				4096 //JFB instead of MAX_INI_SECTION (too large)
#define SET_ACTION_HELP_FILE_MSG	0xF001
#define UPDATE_TIMER				1
#define UPDATE_FREQ					150

enum {
  BUTTONID_LOCK=2000, //JFB would be great to have _APS_NEXT_CONTROL_VALUE *always* defined
  COMBOID_TYPE,
  TXTID_LABEL,
  BUTTONID_ALR,
  BUTTONID_ACTIONLIST,
  BUTTONID_IMPORT_SUB,
  BUTTONID_EXPORT_SUB,
  TXTID_BIG_NOTES
};

enum {
  REQUEST_REFRESH=0,
  NO_REFRESH
};


SNM_NotesHelpWnd* g_pNotesHelpWnd = NULL;
SWSProjConfig<WDL_PtrList_DeleteOnDestroy<SNM_TrackNotes> > g_pTrackNotes;
SWSProjConfig<WDL_PtrList_DeleteOnDestroy<SNM_RegionSubtitle> > g_pRegionSubtitles; // for markers too..
SWSProjConfig<WDL_FastString> g_prjNotes;

int g_notesViewType = -1;
int g_prevNotesViewType = -1;
bool g_locked = false;
char g_lastText[MAX_HELP_LENGTH] = "";
char g_lastImportSubFn[BUFFER_SIZE] = "";
char g_lastExportSubFn[BUFFER_SIZE] = "";
char g_actionHelpFn[BUFFER_SIZE] = "";
char g_notesBigFontName[64] = SWSDLG_TYPEFACE;

// used for action help updates tracking
//JFB TODO: cleanup when we'll be able to access all sections & custom ids
int g_lastActionListSel = -1;
int g_lastActionListCmd = 0;
char g_lastActionSection[SNM_MAX_SECTION_NAME_LEN] = "";
char g_lastActionCustId[SNM_MAX_ACTION_CUSTID_LEN] = "";
char g_lastActionDesc[SNM_MAX_ACTION_NAME_LEN] = ""; 

// other vars for updates tracking
double g_lastMarkerPos = -1.0;
int g_lastMarkerRegionId = -1;
MediaItem* g_mediaItemNote = NULL;
MediaTrack* g_trNote = NULL;

// to distinguish internal marker/region updates from external ones
bool g_internalMkrRgnChange = false;


///////////////////////////////////////////////////////////////////////////////
// SNM_NotesHelpWnd
///////////////////////////////////////////////////////////////////////////////

SNM_NotesHelpWnd::SNM_NotesHelpWnd()
#ifdef _WIN32
	: SWS_DockWnd(IDD_SNM_NOTES, __LOCALIZE("Notes/Subtitles/Help","sws_DLG_152"), "SnMNotesHelp", 30007, SWSGetCommandID(OpenNotesHelpView))
#else
	: SWS_DockWnd(IDD_SNM_NOTES, __LOCALIZE("Notes/Subtitles","sws_DLG_152"), "SnMNotesHelp", 30007, SWSGetCommandID(OpenNotesHelpView))
#endif
{
	// must call SWS_DockWnd::Init() to restore parameters and open the window if necessary
	Init();
}

SNM_NotesHelpWnd::~SNM_NotesHelpWnd()
{
}

void SNM_NotesHelpWnd::OnInitDlg()
{
	m_resize.init_item(IDC_EDIT, 0.0, 0.0, 1.0, 1.0);
	SetWindowLongPtr(GetDlgItem(m_hwnd, IDC_EDIT), GWLP_USERDATA, 0xdeadf00b);

	// WDL GUI init
	m_vwnd_painter.SetGSC(WDL_STYLE_GetSysColor);
    m_parentVwnd.SetRealParent(m_hwnd);

	m_btnLock.SetID(BUTTONID_LOCK);
	m_parentVwnd.AddChild(&m_btnLock);

	m_cbType.SetID(COMBOID_TYPE);
	m_cbType.AddItem(__LOCALIZE("Project notes","sws_DLG_152"));
	m_cbType.AddItem(__LOCALIZE("Item notes","sws_DLG_152"));
	m_cbType.AddItem(__LOCALIZE("Track notes","sws_DLG_152"));
	m_cbType.AddItem(__LOCALIZE("Marker/Region names","sws_DLG_152"));
	m_cbType.AddItem(__LOCALIZE("Marker/Region subtitles","sws_DLG_152"));
#ifdef _WIN32
	m_cbType.AddItem(__LOCALIZE("Action help","sws_DLG_152"));
#endif
	m_parentVwnd.AddChild(&m_cbType);
	// ...the selected item is set through SetType() below

	m_txtLabel.SetID(TXTID_LABEL);
	m_parentVwnd.AddChild(&m_txtLabel);

	m_btnAlr.SetID(BUTTONID_ALR);
	m_parentVwnd.AddChild(&m_btnAlr);

	m_btnActionList.SetID(BUTTONID_ACTIONLIST);
	m_parentVwnd.AddChild(&m_btnActionList);

	m_btnImportSub.SetID(BUTTONID_IMPORT_SUB);
	m_parentVwnd.AddChild(&m_btnImportSub);

	m_btnExportSub.SetID(BUTTONID_EXPORT_SUB);
	m_parentVwnd.AddChild(&m_btnExportSub);

	m_bigNotes.SetID(TXTID_BIG_NOTES);
	m_bigNotes.SetFontName(g_notesBigFontName);
	m_parentVwnd.AddChild(&m_bigNotes);

	g_prevNotesViewType = -1; // will force refresh
	SetType(BOUNDED(g_notesViewType, 0, m_cbType.GetCount()-1)); // + Update()

/* see OnTimer()
	RegisterToMarkerRegionUpdates(&m_mkrRgnSubscriber);
*/
	SetTimer(m_hwnd, UPDATE_TIMER, UPDATE_FREQ, NULL);
}

void SNM_NotesHelpWnd::OnDestroy() 
{
	KillTimer(m_hwnd, UPDATE_TIMER);
/* see OnTimer()
	UnregisterToMarkerRegionUpdates(&m_mkrRgnSubscriber);
*/
	g_prevNotesViewType = -1;
	m_cbType.Empty();
}

// note: no diff with current type, init would fail otherwise
void SNM_NotesHelpWnd::SetType(int _type)
{
	g_notesViewType = _type;
	m_cbType.SetCurSel(g_notesViewType);
	SendMessage(m_hwnd, WM_SIZE, 0, 0); // to update the bottom of the GUI

	// force an initial refresh (when IDC_EDIT has the focus, re-enabling the timer 
	// isn't enough: Update() is skipped, see OnTimer() & IsActive()
	Update();
}

void SNM_NotesHelpWnd::SetText(const char* _str, bool _addRN) {
	if (_str) {
		if (_addRN) GetStringWithRN(_str, g_lastText, MAX_HELP_LENGTH);
		else lstrcpyn(g_lastText, _str, MAX_HELP_LENGTH);
		SetDlgItemText(m_hwnd, IDC_EDIT, g_lastText);
	}
}

void SNM_NotesHelpWnd::RefreshGUI() 
{
	bool bHide = true;
	switch(g_notesViewType)
	{
		case SNM_NOTES_PROJECT:
			bHide = false;
			break;
		case SNM_NOTES_ITEM:
			if (g_mediaItemNote)
				bHide = false;
			break;
		case SNM_NOTES_TRACK:
			if (g_trNote)
				bHide = false;
			break;
		case SNM_NOTES_REGION_NAME:
		case SNM_NOTES_REGION_SUBTITLES:
			if (g_lastMarkerRegionId > 0)
				bHide = false;
			break;
		case SNM_NOTES_ACTION_HELP:
			if (g_lastActionListSel >= 0)
				bHide = false; // for copy/paste even if "Custom: ..."
			break;
	}
	ShowWindow(GetDlgItem(m_hwnd, IDC_EDIT), bHide || g_locked ? SW_HIDE : SW_SHOW);
	m_parentVwnd.RequestRedraw(NULL); // the meat!
}

void SNM_NotesHelpWnd::CSurfSetTrackTitle() {
	if (g_notesViewType == SNM_NOTES_TRACK)
		RefreshGUI();
}

// this is our only notification of active project tab change, so update everything
// (ScheduledJob because of multi-notifs)
void SNM_NotesHelpWnd::CSurfSetTrackListChange() {
	AddOrReplaceScheduledJob(new SNM_NoteHelp_UpdateJob());
}

void SNM_NotesHelpWnd::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (LOWORD(wParam))
	{
		case IDC_EDIT:
			if (HIWORD(wParam)==EN_CHANGE)
				SaveCurrentText(g_notesViewType); // + undo
			break;
		case SET_ACTION_HELP_FILE_MSG:
			SetActionHelpFilename(NULL);
			break;
		case BUTTONID_LOCK:
			if (!HIWORD(wParam))
				ToggleLock();
			break;
		case BUTTONID_ALR:
			if (!HIWORD(wParam) && *g_lastActionCustId && !IsMacro(g_lastActionDesc))
			{
				char link[256] = "";
				char sectionURL[SNM_MAX_SECTION_NAME_LEN] = "";
				if (GetSectionNameAsURL(true, g_lastActionSection, sectionURL, SNM_MAX_SECTION_NAME_LEN))
					if (_snprintfStrict(link, sizeof(link), "http://www.cockos.com/wiki/index.php/%s_%s", sectionURL, g_lastActionCustId) > 0)
						ShellExecute(m_hwnd, "open", link , NULL, NULL, SW_SHOWNORMAL);
			}
			else
				ShellExecute(m_hwnd, "open", "http://wiki.cockos.com/wiki/index.php/Action_List_Reference" , NULL, NULL, SW_SHOWNORMAL);
			break;
		case BUTTONID_ACTIONLIST:
			Main_OnCommand(40605, 0);
			break;
		case BUTTONID_IMPORT_SUB:
			ImportSubTitleFile(NULL);
			break;
		case BUTTONID_EXPORT_SUB:
			ExportSubTitleFile(NULL);
			break;
		case COMBOID_TYPE:
			if (HIWORD(wParam)==CBN_SELCHANGE) {
				SetType(m_cbType.GetCurSel());
				if (!g_locked)
					SetFocus(GetDlgItem(m_hwnd, IDC_EDIT));
			}
			break;
		default:
			Main_OnCommand((int)wParam, (int)lParam);
			break;
	}
}

// bWantEdit ignored: no list view in there
bool SNM_NotesHelpWnd::IsActive(bool bWantEdit) {
	return (IsValidWindow() && (GetForegroundWindow() == m_hwnd || GetFocus() == GetDlgItem(m_hwnd, IDC_EDIT)));
}

HMENU SNM_NotesHelpWnd::OnContextMenu(int x, int y, bool* wantDefaultItems)
{
	if (g_notesViewType == SNM_NOTES_ACTION_HELP)
	{
		HMENU hMenu = CreatePopupMenu();
		AddToMenu(hMenu, __LOCALIZE("Set action help file...","sws_DLG_152"), SET_ACTION_HELP_FILE_MSG);
		return hMenu;
	}
	return NULL;
}

// returns: 
// -1: catch and send to the control, 
//  0: pass-thru to main window (then -666 in SWS_DockWnd::keyHandler())
//  1: eat
int SNM_NotesHelpWnd::OnKey(MSG* msg, int iKeyState) 
{
	if (GetDlgItem(m_hwnd, IDC_EDIT) == msg->hwnd)
	{
		if (g_locked) {
			msg->hwnd = m_hwnd; // redirect to main window
			return 0;
		}
		else if ((msg->message == WM_KEYDOWN || msg->message == WM_CHAR) && msg->wParam == VK_RETURN)
			return -1; // send the return key to the edit control
#ifdef _WIN32
		else if ((msg->message == WM_KEYDOWN || msg->message == WM_CHAR) && msg->wParam == 'A' && iKeyState == LVKF_CONTROL) {
			SendMessage(msg->hwnd, EM_SETSEL, 0, -1); // ctrl+A => select all
			return 1;
		}
#endif
	}
	return 0; 
}

void SNM_NotesHelpWnd::OnTimer(WPARAM wParam)
{
	if (wParam == UPDATE_TIMER)
	{
		// register to marker and region updates only when needed (less stress for REAPER)
		switch (g_notesViewType)
		{
			case SNM_NOTES_REGION_SUBTITLES:
			case SNM_NOTES_REGION_NAME:
				RegisterToMarkerRegionUpdates(&m_mkrRgnSubscriber); // no-op if alreday registered
				break;
			default:
				UnregisterToMarkerRegionUpdates(&m_mkrRgnSubscriber);
				break;
		}

		// no update when editing text or when the view is hidden (e.g. inactive docker tab).
		// when the view is active: update only for markers and if the view is locked 
		// => updates during playback, in other cases (e.g. item selection change) the main 
		// window will be the active one
		if (g_notesViewType != SNM_NOTES_PROJECT && IsWindowVisible(m_hwnd) && (!IsActive() || 
			(g_locked && (g_notesViewType == SNM_NOTES_REGION_SUBTITLES || g_notesViewType == SNM_NOTES_REGION_NAME))))
		{
			Update();
		}
	}
}

void SNM_NotesHelpWnd::OnResize()
{
	if (g_notesViewType != g_prevNotesViewType)
	{
		if (g_notesViewType == SNM_NOTES_REGION_SUBTITLES || g_notesViewType == SNM_NOTES_ACTION_HELP)
			m_resize.get_item(IDC_EDIT)->orig.bottom = m_resize.get_item(IDC_EDIT)->real_orig.bottom - 30;
		else
			memcpy(&m_resize.get_item(IDC_EDIT)->orig, &m_resize.get_item(IDC_EDIT)->real_orig, sizeof(RECT));
		InvalidateRect(m_hwnd, NULL, 0);
	}
}

void SNM_NotesHelpWnd::DrawControls(LICE_IBitmap* _bm, const RECT* _r, int* _tooltipHeight)
{
	int h=SNM_TOP_GUI_HEIGHT;
	if (_tooltipHeight)
		*_tooltipHeight = h;

	// "big" notes (dynamic font size)
	// drawn first so that it is displayed even with tiny sizing..
	if (g_locked)
	{
		// work on a copy rather than g_lastText (will get modified)
		char buf[MAX_HELP_LENGTH] = "";
		GetDlgItemText(m_hwnd, IDC_EDIT, buf, MAX_HELP_LENGTH);
		if (*buf)
		{
			if (g_notesViewType == SNM_NOTES_REGION_NAME)
				ShortenStringToFirstRN(buf);

			RECT r = *_r;
			r.top += h;
			m_bigNotes.SetPosition(&r);
			m_bigNotes.SetText(buf);
			m_bigNotes.SetVisible(true);
		}
	}
	// clear last "big notes"
	else
		LICE_FillRect(_bm,0,h,_bm->getWidth(),_bm->getHeight()-h,WDL_STYLE_GetSysColor(COLOR_WINDOW),0.0,LICE_BLIT_MODE_COPY);


	// 1st row of controls

	LICE_CachedFont* font = SNM_GetThemeFont();
	IconTheme* it = SNM_GetIconTheme();
	int x0=_r->left+10;

	// lock button
	SNM_SkinButton(&m_btnLock, it ? &it->toolbar_lock[!g_locked] : NULL, g_locked ? __LOCALIZE("Unlock","sws_DLG_152") : __LOCALIZE("Lock","sws_DLG_152"));
	if (SNM_AutoVWndPosition(DT_LEFT, &m_btnLock, NULL, _r, &x0, _r->top, h))
	{
		// view type
		m_cbType.SetFont(font);
		if (SNM_AutoVWndPosition(DT_LEFT, &m_cbType, NULL, _r, &x0, _r->top, h))
		{
			// label
			char str[512];
			lstrcpyn(str, __LOCALIZE("No selection!","sws_DLG_152"), 512);
			switch(g_notesViewType)
			{
				case SNM_NOTES_PROJECT:
					EnumProjects(-1, str, 512);
					break;
				case SNM_NOTES_ITEM:
					if (g_mediaItemNote)
					{
						MediaItem_Take* tk = GetActiveTake(g_mediaItemNote);
						char* tkName= tk ? (char*)GetSetMediaItemTakeInfo(tk, "P_NAME", NULL) : NULL;
						lstrcpyn(str, tkName?tkName:"", 512);
					}
					break;
				case SNM_NOTES_TRACK:
					if (g_trNote)
					{
						int id = CSurf_TrackToID(g_trNote, false);
						if (id > 0) {
							char* trName = (char*)GetSetMediaTrackInfo(g_trNote, "P_NAME", NULL);
							_snprintfSafe(str, sizeof(str), "[%d] \"%s\"", id, trName?trName:"");
						}
						else if (id == 0)
							strcpy(str, __LOCALIZE("[MASTER]","sws_DLG_152"));
					}
					break;
				case SNM_NOTES_REGION_NAME:
				case SNM_NOTES_REGION_SUBTITLES:
					if (g_lastMarkerRegionId <= 0 || !EnumMarkerRegionDesc(GetMarkerRegionIndexFromId(g_lastMarkerRegionId), str, 512, SNM_MARKER_MASK|SNM_REGION_MASK, g_notesViewType == SNM_NOTES_REGION_SUBTITLES) || !*str)
						lstrcpyn(str, __LOCALIZE("No marker or region at play/edit cursor!","sws_DLG_152"), 512);
					break;
				case SNM_NOTES_ACTION_HELP:
					if (g_lastActionDesc && *g_lastActionDesc && g_lastActionSection && *g_lastActionSection)
						_snprintfSafe(str, sizeof(str), " [%s] %s", g_lastActionSection, g_lastActionDesc);
/* API LIMITATION: use smthg like that when we will be able to access all sections
						lstrcpyn(str, kbd_getTextFromCmd(g_lastActionListCmd, NULL), 512);
*/
					break;
			}

			m_txtLabel.SetFont(font);
			m_txtLabel.SetText(str);
			if (SNM_AutoVWndPosition(DT_LEFT, &m_txtLabel, NULL, _r, &x0, _r->top, h))
				SNM_AddLogo(_bm, _r, x0, h);
		}
	}


	// 2nd row of controls
	if (g_locked)
		return;

	x0 = _r->left+8; h=SNM_BOT_GUI_HEIGHT;
	int y0 = _r->bottom-h;

	// import/export buttons
	if (g_notesViewType == SNM_NOTES_REGION_SUBTITLES)
	{
		SNM_SkinToolbarButton(&m_btnImportSub, __LOCALIZE("Import...","sws_DLG_152"));
		if (!SNM_AutoVWndPosition(DT_LEFT, &m_btnImportSub, NULL, _r, &x0, y0, h, 4))
			return;

		SNM_SkinToolbarButton(&m_btnExportSub, __LOCALIZE("Export...","sws_DLG_152"));
		if (!SNM_AutoVWndPosition(DT_LEFT, &m_btnExportSub, NULL, _r, &x0, y0, h))
			return;
	}

	// online help & action list buttons
	if (g_notesViewType == SNM_NOTES_ACTION_HELP)
	{
		SNM_SkinToolbarButton(&m_btnActionList, __LOCALIZE("Action list...","sws_DLG_152"));
		if (!SNM_AutoVWndPosition(DT_LEFT, &m_btnActionList, NULL, _r, &x0, y0, h, 4))
			return;

		SNM_SkinToolbarButton(&m_btnAlr, __LOCALIZE("Online help...","sws_DLG_152"));
		if (!SNM_AutoVWndPosition(DT_LEFT, &m_btnAlr, NULL, _r, &x0, y0, h))
			return;
	}
}

bool SNM_NotesHelpWnd::GetToolTipString(int _xpos, int _ypos, char* _bufOut, int _bufOutSz)
{
	if (WDL_VWnd* v = m_parentVwnd.VirtWndFromPoint(_xpos,_ypos,1))
	{
		switch (v->GetID())
		{
			case BUTTONID_LOCK:
				return (lstrcpyn(_bufOut, g_locked ? __LOCALIZE("Text locked ('Big font' mode)", "sws_DLG_152") : __LOCALIZE("Text unlocked", "sws_DLG_152"), _bufOutSz) != NULL);
			case COMBOID_TYPE:
				return (lstrcpyn(_bufOut, __LOCALIZE("Notes type","sws_DLG_152"), _bufOutSz) != NULL);
			case TXTID_LABEL:
				return (lstrcpyn(_bufOut, ((WDL_VirtualStaticText*)v)->GetText(), _bufOutSz) != NULL);
		}
	}
	return false;
}

void SNM_NotesHelpWnd::ToggleLock()
{
	g_locked = !g_locked;
	RefreshToolbar(NamedCommandLookup("_S&M_ACTIONHELPTGLOCK"));
	if (g_notesViewType == SNM_NOTES_REGION_SUBTITLES || g_notesViewType == SNM_NOTES_REGION_NAME)
		Update(true); // play vs edit cursor when unlocking
	else
		 RefreshGUI();

	if (!g_locked)
		SetFocus(GetDlgItem(m_hwnd, IDC_EDIT));
}


///////////////////////////////////////////////////////////////////////////////

void SNM_NotesHelpWnd::SaveCurrentText(int _type) 
{
	switch(_type) 
	{
		case SNM_NOTES_PROJECT:
			SaveCurrentPrjNotes();
			break;
		case SNM_NOTES_ITEM: 
			SaveCurrentItemNotes(); 
			break;
		case SNM_NOTES_TRACK:
			SaveCurrentTrackNotes();
			break;
		case SNM_NOTES_REGION_NAME:
		case SNM_NOTES_REGION_SUBTITLES:
			SaveCurrentMkrRgnNameOrNotes(_type != SNM_NOTES_REGION_SUBTITLES);
			break;
		case SNM_NOTES_ACTION_HELP:
			SaveCurrentHelp();
			break;
	}
}

void SNM_NotesHelpWnd::SaveCurrentPrjNotes()
{
	GetDlgItemText(m_hwnd, IDC_EDIT, g_lastText, MAX_HELP_LENGTH);
	g_prjNotes.Get()->Set(g_lastText); // CRLF removed only when saving the project..
	Undo_OnStateChangeEx(__LOCALIZE("Edit project notes","sws_undo"), UNDO_STATE_MISCCFG, -1);
}

void SNM_NotesHelpWnd::SaveCurrentHelp()
{
	if (*g_lastActionCustId && !IsMacro(g_lastActionDesc)) {
		GetDlgItemText(m_hwnd, IDC_EDIT, g_lastText, MAX_HELP_LENGTH);
		SaveHelp(g_lastActionCustId, g_lastText);
	}
}

void SNM_NotesHelpWnd::SaveCurrentItemNotes()
{
	if (g_mediaItemNote && GetMediaItem_Track(g_mediaItemNote))
	{
		GetDlgItemText(m_hwnd, IDC_EDIT, g_lastText, MAX_HELP_LENGTH);
		if (GetSetMediaItemInfo(g_mediaItemNote, "P_NOTES", g_lastText))
		{
//				UpdateItemInProject(g_mediaItemNote);
			UpdateTimeline(); // for the item's note button 
			Undo_OnStateChangeEx(__LOCALIZE("Edit item notes","sws_undo"), UNDO_STATE_ALL, -1); //JFB TODO? -1 to replace? UNDO_STATE_ITEMS?
		}
	}
}

void SNM_NotesHelpWnd::SaveCurrentTrackNotes()
{
	if (g_trNote && CSurf_TrackToID(g_trNote, false) >= 0)
	{
		GetDlgItemText(m_hwnd, IDC_EDIT, g_lastText, MAX_HELP_LENGTH);
		bool found = false;
		for (int i=0; i < g_pTrackNotes.Get()->GetSize(); i++) 
		{
			if (g_pTrackNotes.Get()->Get(i)->m_tr == g_trNote) {
				g_pTrackNotes.Get()->Get(i)->m_notes.Set(g_lastText); // CRLF removed only when saving the project..
				found = true;
				break;
			}
		}
		if (!found)
			g_pTrackNotes.Get()->Add(new SNM_TrackNotes(g_trNote, g_lastText));
		Undo_OnStateChangeEx(__LOCALIZE("Edit track notes","sws_undo"), UNDO_STATE_MISCCFG, -1); //JFB TODO? -1 to replace?
	}
}

void SNM_NotesHelpWnd::SaveCurrentMkrRgnNameOrNotes(bool _name)
{
	if (g_lastMarkerRegionId > 0)
	{
		GetDlgItemText(m_hwnd, IDC_EDIT, g_lastText, MAX_HELP_LENGTH);
		if (_name)
		{
			int idx = GetMarkerRegionIndexFromId(g_lastMarkerRegionId);
			if (idx >= 0)
			{
				double pos, end; int num, color; bool isRgn;
				if (EnumProjectMarkers3(NULL, idx, &isRgn, &pos, &end, NULL, &num, &color))
				{
					ShortenStringToFirstRN(g_lastText);

					// track marker/region name updates that will lead to reentrance notifs
					// via SNM_MarkerRegionSubscriber.NotifyMarkerRegionUpdate()
					g_internalMkrRgnChange = true;

					if (SNM_SetProjectMarker(NULL, num, isRgn, pos, end, g_lastText, color)) {
						UpdateTimeline();
						Undo_OnStateChangeEx(isRgn ? __LOCALIZE("Edit region name","sws_undo") : __LOCALIZE("Edit marker name","sws_undo"), UNDO_STATE_ALL, -1);
					}
				}
			}
		}
		// subtitles
		else
		{
			// CRLF removed only when saving the project..
			bool found = false;
			for (int i=0; i < g_pRegionSubtitles.Get()->GetSize(); i++) 
			{
				if (g_pRegionSubtitles.Get()->Get(i)->m_id == g_lastMarkerRegionId) {
					g_pRegionSubtitles.Get()->Get(i)->m_notes.Set(g_lastText);
					found = true;
					break;
				}
			}
			if (!found)
				g_pRegionSubtitles.Get()->Add(new SNM_RegionSubtitle(g_lastMarkerRegionId, g_lastText));
			Undo_OnStateChangeEx(IsRegion(g_lastMarkerRegionId) ? __LOCALIZE("Edit region subtitle","sws_undo") : __LOCALIZE("Edit marker subtitle","sws_undo"), UNDO_STATE_MISCCFG, -1);
		}
	}
}


///////////////////////////////////////////////////////////////////////////////

void SNM_NotesHelpWnd::Update(bool _force)
{
	static bool bRecurseCheck = false;
	if (bRecurseCheck)
		return;

	bRecurseCheck = true; 

	// force refresh if needed
	if (_force || g_notesViewType != g_prevNotesViewType)
	{
		g_prevNotesViewType = g_notesViewType;
		g_lastActionListSel = -1;
		*g_lastActionCustId = '\0';
		*g_lastActionDesc = '\0';
		g_lastActionListCmd = 0;
		*g_lastActionSection = '\0';
		g_mediaItemNote = NULL;
		g_trNote = NULL;
		g_lastMarkerPos = -1.0;
		g_lastMarkerRegionId = -1;
		_force = true; // trick for RefreshGUI() below..
	}

	// update
	int refreshType = NO_REFRESH;
	switch(g_notesViewType)
	{
		case SNM_NOTES_PROJECT:
			SetText(g_prjNotes.Get()->Get());
			refreshType = REQUEST_REFRESH;
			break;
		case SNM_NOTES_ITEM:
			refreshType = UpdateItemNotes();
			break;
		case SNM_NOTES_TRACK:
			refreshType = UpdateTrackNotes();
			break;
		case SNM_NOTES_REGION_NAME:
		case SNM_NOTES_REGION_SUBTITLES:
			refreshType = UpdateMkrRgnNameOrNotes(g_notesViewType != SNM_NOTES_REGION_SUBTITLES);
			break;
		case SNM_NOTES_ACTION_HELP:
			refreshType = UpdateActionHelp();
			break;
	}
	
	if (_force || refreshType == REQUEST_REFRESH)
		RefreshGUI();

	bRecurseCheck = false;
}

int SNM_NotesHelpWnd::UpdateActionHelp()
{
	int refreshType = NO_REFRESH;
	int iSel = GetSelectedAction(
		g_lastActionSection, SNM_MAX_SECTION_NAME_LEN, &g_lastActionListCmd, 
		g_lastActionCustId, SNM_MAX_ACTION_CUSTID_LEN, 
		g_lastActionDesc, SNM_MAX_ACTION_NAME_LEN);

	if (iSel >= 0)
	{
		if (iSel != g_lastActionListSel)
		{
			g_lastActionListSel = iSel;
			if (*g_lastActionCustId && *g_lastActionDesc)
			{
				if (IsMacro(g_lastActionDesc))
					SetText(g_lastActionCustId);
				else
				{
					char buf[MAX_HELP_LENGTH] = "";
					LoadHelp(g_lastActionCustId, buf, MAX_HELP_LENGTH);
					SetText(buf);
				}
				refreshType = REQUEST_REFRESH;
			}
		}
	}
	else if (g_lastActionListSel>=0 || *g_lastText) // *g_lastText: when switching note types w/o nothing selected for the new type
	{
		g_lastActionListSel = -1;
		*g_lastActionCustId = '\0';
		*g_lastActionDesc = '\0';
		g_lastActionListCmd = 0;
		*g_lastActionSection = '\0';
		SetText("");
		refreshType = REQUEST_REFRESH;
	}
	return refreshType;
}

int SNM_NotesHelpWnd::UpdateItemNotes()
{
	int refreshType = NO_REFRESH;
	if (MediaItem* selItem = GetSelectedMediaItem(NULL, 0))
	{
		if (selItem != g_mediaItemNote)
		{
			g_mediaItemNote = selItem;
			if (char* notes = (char*)GetSetMediaItemInfo(g_mediaItemNote, "P_NOTES", NULL))
				SetText(notes, false);
			refreshType = REQUEST_REFRESH;
		} 
	}
	else if (g_mediaItemNote || *g_lastText)
	{
		g_mediaItemNote = NULL;
		SetText("");
		refreshType = REQUEST_REFRESH;
	}

/*JFB commented: manages concurent item note updates but kludgy..
#ifdef _WIN32
	if (refreshType == NO_REFRESH)
	{
		char name[BUFFER_SIZE] = "";
		MediaItem_Take* tk = GetActiveTake(g_mediaItemNote);
		char* tkName = tk ? (char*)GetSetMediaItemTakeInfo(tk, "P_NAME", NULL) : NULL;
		_snprintfSafe(name, sizeof(name), "%s \"%s\"", __localizeFunc("Notes for", "notes", 0), tkName ? tkName : __localizeFunc("Empty Item", "common", 0));
		if (HWND w = GetReaWindowByTitle(name))
			g_mediaItemNote = NULL; // will force refresh
	}
#endif
*/
	return refreshType;
}

int SNM_NotesHelpWnd::UpdateTrackNotes()
{
	int refreshType = NO_REFRESH;
	if (MediaTrack* selTr = SNM_GetSelectedTrack(NULL, 0, true))
	{
		if (selTr != g_trNote)
		{
			g_trNote = selTr;

			for (int i=0; i < g_pTrackNotes.Get()->GetSize(); i++)
				if (g_pTrackNotes.Get()->Get(i)->m_tr == g_trNote) {
					SetText(g_pTrackNotes.Get()->Get(i)->m_notes.Get());
					return REQUEST_REFRESH;
				}

			g_pTrackNotes.Get()->Add(new SNM_TrackNotes(g_trNote, ""));
			SetText("");
			refreshType = REQUEST_REFRESH;
		} 
	}
	else if (g_trNote || *g_lastText)
	{
		g_trNote = NULL;
		SetText("");
		refreshType = REQUEST_REFRESH;
	}
	return refreshType;
}

int SNM_NotesHelpWnd::UpdateMkrRgnNameOrNotes(bool _name)
{
	int refreshType = NO_REFRESH;

	double dPos=GetCursorPositionEx(NULL), accuracy=SNM_FUDGE_FACTOR;
	if (g_locked && GetPlayState()) { // playing & update required?
		dPos = GetPlayPosition();
		accuracy = 0.1; // do not stress REAPER
	}

	if (fabs(g_lastMarkerPos-dPos) > accuracy)
	{
		g_lastMarkerPos = dPos;

		int id, idx = FindMarkerRegion(dPos, SNM_MARKER_MASK|SNM_REGION_MASK, &id);
		if (id > 0)
		{
			if (id != g_lastMarkerRegionId)
			{
				g_lastMarkerRegionId = id;
				if (_name)
				{
					char* name = NULL;
					EnumProjectMarkers2(NULL, idx, NULL, NULL, NULL, &name, NULL);
					SetText(name ? name : "");
				}
				else // subtitle
				{
					for (int i=0; i < g_pRegionSubtitles.Get()->GetSize(); i++)
					{
						if (g_pRegionSubtitles.Get()->Get(i)->m_id == id) {
							SetText(g_pRegionSubtitles.Get()->Get(i)->m_notes.Get());
							return REQUEST_REFRESH;
						}
					}
					g_pRegionSubtitles.Get()->Add(new SNM_RegionSubtitle(id, ""));
					SetText("");
				}
				refreshType = REQUEST_REFRESH;
			}
		}
		else if (g_lastMarkerRegionId>0 || *g_lastText)
		{
			g_lastMarkerPos = -1.0;
			g_lastMarkerRegionId = -1;
			SetText("");
			refreshType = REQUEST_REFRESH;
		}
	}
	return refreshType;
}


///////////////////////////////////////////////////////////////////////////////

// load/save action help (from/to ini file)
// note: WDL's cfg_encode_textblock() & decode_textblock() will not help here..

void LoadHelp(const char* _cmdName, char* _buf, int _bufSize)
{
	if (_cmdName && *_cmdName && _buf)
	{
		memset(_buf, 0, _bufSize);

		// note: "size" includes trailing '\0', "buf" is double null terminated
		char buf[MAX_HELP_LENGTH] = "";
		int j=0, size = GetPrivateProfileSection(_cmdName,buf,MAX_HELP_LENGTH,g_actionHelpFn);
		for (int i=0; i < (size-1) && i < (_bufSize-2); i++) {
			if (!buf[i]) _buf[j++] = '\n'; // done this way for ascendant compatibilty reasons..
			else if (buf[i] != '|') _buf[j++] = buf[i]; // oh, well..
		}
	}
}

// adds '|' at start of line 
// (allows empty lines which would be scratched by GetPrivateProfileSection() otherwise)
void SaveHelp(const char* _cmdName, const char* _help)
{
	if (_cmdName && *_cmdName && _help) 
	{
		char buf[MAX_HELP_LENGTH] = "";
		memset(buf, 0, MAX_HELP_LENGTH);

		if (*_help)
		{
			*buf = '|';

			int i=0, j=1;
			while (_help[i] && j < (MAX_HELP_LENGTH-2))
			{
				if (_help[i] != '\r') // skip
				{
					buf[j++] = _help[i];
					if (_help[i] == '\n')
						buf[j++] = '|';
				}
				i++;
			}
		}
		WritePrivateProfileStruct(_cmdName, NULL, NULL, 0, g_actionHelpFn); // flush section
		WritePrivateProfileSection(_cmdName, buf, g_actionHelpFn);
	}
}

void SetActionHelpFilename(COMMAND_T*) {
	if (char* fn = BrowseForFiles(__LOCALIZE("S&M - Set action help file","sws_DLG_152"), g_actionHelpFn, NULL, false, SNM_INI_EXT_LIST)) {
		lstrcpyn(g_actionHelpFn, fn, BUFFER_SIZE);
		free(fn);
	}
}


///////////////////////////////////////////////////////////////////////////////

// encode/decode notes (to/from RPP chunks)
// note: WDL's cfg_encode_textblock() & cfg_decode_textblock() will not help here..

bool GetStringFromNotesChunk(WDL_FastString* _notesIn, char* _bufOut, int _bufOutSz)
{
	if (!_bufOut || !_notesIn)
		return false;

	memset(_bufOut, 0, _bufOutSz);
	const char* pNotes = _notesIn->Get();
	if (pNotes && *pNotes)
	{
		// find 1st '|'
		int i=0; while (pNotes[i] && pNotes[i] != '|') i++;
		if (pNotes[i]) i++;
		else return true;
	
		int j=0;
		while (pNotes[i] && j < _bufOutSz)
		{
			if (pNotes[i] != '|' || pNotes[i-1] != '\n') // i is >0 here
				_bufOut[j++] = pNotes[i];
			i++;
		}
		if (j>=3 && !strcmp(_bufOut+j-3, "\n>\n")) // remove trailing "\n>\n"
			_bufOut[j-3] = '\0'; 
	}
	return true;
}

bool GetNotesChunkFromString(const char* _bufIn, WDL_FastString* _notesOut, const char* _startLine)
{
	if (_notesOut && _bufIn)
	{
		if (!_startLine) _notesOut->Set("<NOTES\n|");
		else _notesOut->Set(_startLine);

		int i=0;
		while (_bufIn[i])
		{
			if (_bufIn[i] == '\n') 
				_notesOut->Append("\n|");
			else if (_bufIn[i] != '\r') 
				_notesOut->Append(_bufIn+i, 1);
			i++;
		}
		_notesOut->Append("\n>\n");
		return true;
	}
	return false;
}


///////////////////////////////////////////////////////////////////////////////

void SNM_NoteHelp_UpdateJob::Perform() {
	if (g_pNotesHelpWnd)
		g_pNotesHelpWnd->Update(true);
}


///////////////////////////////////////////////////////////////////////////////

// ScheduledJob because of multi-notifs during project switches (vs CSurfSetTrackListChange)
void SNM_NoteHelp_MarkerRegionSubscriber::NotifyMarkerRegionUpdate(int _updateFlags)
{
	if (g_notesViewType == SNM_NOTES_REGION_SUBTITLES)
		AddOrReplaceScheduledJob(new SNM_NoteHelp_UpdateJob());
	else if (g_notesViewType == SNM_NOTES_REGION_NAME)
	{
		if (g_internalMkrRgnChange)
			g_internalMkrRgnChange = false;
		else
			AddOrReplaceScheduledJob(new SNM_NoteHelp_UpdateJob());
	}
}


///////////////////////////////////////////////////////////////////////////////

// import/export subtitle files, only SubRip (.srt) files atm
// see http://en.wikipedia.org/wiki/SubRip#Specifications

bool ImportSubRipFile(const char* _fn)
{
	bool ok = false;
	double firstPos = -1.0;

	// no need to check extension here, it's done for us
	if (FILE* f = fopenUTF8(_fn, "rt"))
	{
		char buf[1024];
		while(fgets(buf, sizeof(buf), f) && *buf)
		{
			if (int num = atoi(buf))
			{
				if (fgets(buf, sizeof(buf), f) && *buf)
				{
					int p1[4], p2[4];
					if (sscanf(buf, "%d:%d:%d,%d --> %d:%d:%d,%d", 
						&p1[0], &p1[1], &p1[2], &p1[3], 
						&p2[0], &p2[1], &p2[2], &p2[3]) != 8)
					{
						break;
					}

					WDL_FastString notes;
					while (fgets(buf, sizeof(buf), f) && *buf) {
						if (buf[0] == '\n') break;
						notes.Append(buf);
					}

					if (_snprintfStrict(buf, sizeof(buf), "Subtitle #%d", num) <= 0)
						*buf = '\0';

					num = AddProjectMarker(NULL, true, 
						p1[0]*3600 + p1[1]*60 + p1[2] + double(p1[3])/1000, 
						p2[0]*3600 + p2[1]*60 + p2[2] + double(p2[3])/1000, 
						buf, num);

					if (num >= 0)
					{
						ok = true; // region added (at least)

						if (firstPos < 0.0)
							firstPos = p1[0]*3600 + p1[1]*60 + p1[2] + double(p1[3])/1000;

						int id = MakeMarkerRegionId(num, true);
						if (id > 0) // add the sub, no duplicate mgmt..
							g_pRegionSubtitles.Get()->Add(new SNM_RegionSubtitle(id, notes.Get()));
					}
				}
				else
					break;
			}
		}
		fclose(f);
	}
	
	if (ok)
	{
		UpdateTimeline(); // redraw the ruler (andd arrange view)
		if (firstPos > 0.0)
			SetEditCurPos(firstPos, true, false);
	}
	return ok;
}

void ImportSubTitleFile(COMMAND_T* _ct)
{
	if (char* fn = BrowseForFiles(__LOCALIZE("S&M - Import subtitle file","sws_DLG_152"), g_lastImportSubFn, NULL, false, SNM_SUB_EXT_LIST))
	{
		lstrcpyn(g_lastImportSubFn, fn, BUFFER_SIZE);
		if (ImportSubRipFile(fn))
			//JFB hard-coded undo label: _ct might be NULL (when called from a button)
			//    + avoid trailing "..." in undo point name (when called from an action)
			Undo_OnStateChangeEx(__LOCALIZE("Import subtitle file","sws_DLG_152"), UNDO_STATE_ALL, -1);
		else
			MessageBox(GetMainHwnd(), __LOCALIZE("Invalid subtitle file!","sws_DLG_152"), __LOCALIZE("S&M - Error","sws_DLG_152"), MB_OK);
		free(fn);
	}
}

bool ExportSubRipFile(const char* _fn)
{
	if (FILE* f = fopenUTF8(_fn, "wt"))
	{
		WDL_FastString subs;
		int x=0, subIdx=1, num; bool isRgn; double p1, p2;
		while (x = EnumProjectMarkers2(NULL, x, &isRgn, &p1, &p2, NULL, &num))
		{
			// special case for markers: end position = next start position
			if (!isRgn)
			{
				int x2=x;
				if (!EnumProjectMarkers2(NULL, x2, NULL, &p2 /* <- the trick */, NULL, NULL, NULL))
					p2 = p1 + 5.0; // best effort..
			}

			int id = MakeMarkerRegionId(num, isRgn);
			if (id > 0)
			{
				for (int i=0; i < g_pRegionSubtitles.Get()->GetSize(); i++)
				{
					SNM_RegionSubtitle* rn = g_pRegionSubtitles.Get()->Get(i);
					if (rn->m_id == id)
					{
						subs.AppendFormatted(64, "%d\n", subIdx++); // subs have their own indexes
						
						int h, m, s, ms;
						TranslatePos(p1, &h, &m, &s, &ms);
						subs.AppendFormatted(64,"%02d:%02d:%02d,%03d --> ",h,m,s,ms);
						TranslatePos(p2, &h, &m, &s, &ms);
						subs.AppendFormatted(64,"%02d:%02d:%02d,%03d\n",h,m,s,ms);

						subs.Append(rn->m_notes.Get());
						if (rn->m_notes.GetLength() && rn->m_notes.Get()[rn->m_notes.GetLength()-1] != '\n')
							subs.Append("\n");
						subs.Append("\n");
					}
				}
			}
		}

		if (subs.GetLength())
		{
			fputs(subs.Get(), f);
			fclose(f);
			return true;
		}
	}
	return false;
}

void ExportSubTitleFile(COMMAND_T* _ct)
{
	char fn[BUFFER_SIZE] = "";
	if (BrowseForSaveFile(__LOCALIZE("S&M - Export subtitle file","sws_DLG_152"), g_lastExportSubFn, strrchr(g_lastExportSubFn, '.') ? g_lastExportSubFn : NULL, SNM_SUB_EXT_LIST, fn, BUFFER_SIZE)) {
		lstrcpyn(g_lastExportSubFn, fn, BUFFER_SIZE);
		ExportSubRipFile(fn);
	}
}


///////////////////////////////////////////////////////////////////////////////

static bool ProcessExtensionLine(const char *line, ProjectStateContext *ctx, bool isUndo, struct project_config_extension_t *reg)
{
	LineParser lp(false);
	if (lp.parse(line) || lp.getnumtokens() < 1)
		return false;

	if (!strcmp(lp.gettoken_str(0), "<S&M_PROJNOTES"))
	{
		WDL_FastString notes;
		ExtensionConfigToString(&notes, ctx);
		char buf[MAX_HELP_LENGTH] = "";
		GetStringFromNotesChunk(&notes, buf, MAX_HELP_LENGTH);
		g_prjNotes.Get()->Set(buf);
		return true;
	}
	else if (!strcmp(lp.gettoken_str(0), "<S&M_TRACKNOTES"))
	{
		GUID g;
		stringToGuid(lp.gettoken_str(1), &g);
		MediaTrack* tr = GuidToTrack(&g);
		if (tr)
		{
			WDL_FastString notes;
			ExtensionConfigToString(&notes, ctx);
			char buf[MAX_HELP_LENGTH] = "";
			if (GetStringFromNotesChunk(&notes, buf, MAX_HELP_LENGTH))
				g_pTrackNotes.Get()->Add(new SNM_TrackNotes(tr, buf));
			return true;
		}
	}
	else if (!strcmp(lp.gettoken_str(0), "<S&M_SUBTITLE"))
	{
		if (GetMarkerRegionIndexFromId(lp.gettoken_int(1)) >= 0) // still exists?
		{
			WDL_FastString notes;
			ExtensionConfigToString(&notes, ctx);
			char buf[MAX_HELP_LENGTH] = "";
			if (GetStringFromNotesChunk(&notes, buf, MAX_HELP_LENGTH))
				g_pRegionSubtitles.Get()->Add(new SNM_RegionSubtitle(lp.gettoken_int(1), buf));
			return true;
		}
	}
	return false;
}

static void SaveExtensionConfig(ProjectStateContext *ctx, bool isUndo, struct project_config_extension_t *reg)
{
	// cleanup: just saves some RAM (as these things won't be saved below anyway)
//JFB	if (!isUndo)
	{
		// delete unused tracks
		for (int i=0; i < g_pTrackNotes.Get()->GetSize(); i++)
			if (CSurf_TrackToID(g_pTrackNotes.Get()->Get(i)->m_tr, false) < 0)
				g_pTrackNotes.Get()->Delete(i--, true);

		// remove non-existant markers & regions
		for (int i=0; i < g_pRegionSubtitles.Get()->GetSize(); i++)
			if (GetMarkerRegionIndexFromId(g_pRegionSubtitles.Get()->Get(i)->m_id) < 0)
				g_pRegionSubtitles.Get()->Delete(i--, true);
	}

	char startLine[SNM_MAX_CHUNK_LINE_LENGTH] = "";
	char strId[128] = "";
	WDL_FastString formatedNotes;

	// save project notes
	if (g_prjNotes.Get()->GetLength())
	{
		strcpy(startLine, "<S&M_PROJNOTES\n|");
		if (GetNotesChunkFromString(g_prjNotes.Get()->Get(), &formatedNotes, startLine))
			StringToExtensionConfig(&formatedNotes, ctx);
	}

	// save track notes
	for (int i=0; i < g_pTrackNotes.Get()->GetSize(); i++)
	{
		if (g_pTrackNotes.Get()->Get(i)->m_notes.GetLength())
		{
			GUID g;
			if (CSurf_TrackToID(g_pTrackNotes.Get()->Get(i)->m_tr, false) > 0)
				g = *(GUID*)GetSetMediaTrackInfo(g_pTrackNotes.Get()->Get(i)->m_tr, "GUID", NULL);
			else
				g = GUID_NULL;
			guidToString(&g, strId);
			if (_snprintfStrict(startLine, sizeof(startLine), "<S&M_TRACKNOTES %s\n|", strId) > 0)
				if (GetNotesChunkFromString(g_pTrackNotes.Get()->Get(i)->m_notes.Get(), &formatedNotes, startLine))
					StringToExtensionConfig(&formatedNotes, ctx);
		}
	}

	// save region/marker notes
	for (int i=0; i < g_pRegionSubtitles.Get()->GetSize(); i++)
	{
		if (g_pRegionSubtitles.Get()->Get(i)->m_notes.GetLength() &&
			GetMarkerRegionIndexFromId(g_pRegionSubtitles.Get()->Get(i)->m_id) >= 0)
		{
			if (_snprintfStrict(startLine, sizeof(startLine), "<S&M_SUBTITLE %d\n|", g_pRegionSubtitles.Get()->Get(i)->m_id) > 0)
				if (GetNotesChunkFromString(g_pRegionSubtitles.Get()->Get(i)->m_notes.Get(), &formatedNotes, startLine))
					StringToExtensionConfig(&formatedNotes, ctx);
		}
	}
}

static void BeginLoadProjectState(bool isUndo, struct project_config_extension_t *reg)
{
	g_prjNotes.Cleanup();
	g_prjNotes.Get()->Set("");

	g_pTrackNotes.Cleanup();
	g_pTrackNotes.Get()->Empty(true);

	g_pRegionSubtitles.Cleanup();
	g_pRegionSubtitles.Get()->Empty(true);

	// init S&M project notes with REAPER's ones
	if (!isUndo)
	{
		char buf[BUFFER_SIZE] = "";
		EnumProjects(-1, buf, BUFFER_SIZE);

		// SWS - It's possible at this point that we're not reading an RPP file (eg during import), check to be sure!
		// If so, just ignore the possibility that there might be project notes.
		if (strlen(buf) > 3 && _stricmp(buf+strlen(buf)-3, "RPP") == 0)
		{
			WDL_FastString startOfrpp;

			// just read the very begining of the file (where prj notes are, no-op if notes are bigger)
			// => much faster REAPER startup (based on the parser tolerance..)
			if (LoadChunk(buf, &startOfrpp, true, MAX_HELP_LENGTH+128) && startOfrpp.GetLength())
			{
				SNM_ChunkParserPatcher p(&startOfrpp);
				WDL_FastString notes;
				if (p.GetSubChunk("NOTES", 2, 0, &notes, "RIPPLE") >= 0)
				{
					char bufNotes[MAX_HELP_LENGTH] = "";
					if (GetStringFromNotesChunk(&notes, bufNotes, MAX_HELP_LENGTH))
						g_prjNotes.Get()->Set(bufNotes);
				}
			}
		}
	}
}

static project_config_extension_t g_projectconfig = {
	ProcessExtensionLine, SaveExtensionConfig, BeginLoadProjectState, NULL
};


///////////////////////////////////////////////////////////////////////////////

int NotesHelpViewInit()
{
	lstrcpyn(g_lastImportSubFn, GetResourcePath(), BUFFER_SIZE);
	lstrcpyn(g_lastExportSubFn, GetResourcePath(), BUFFER_SIZE);

	// load prefs 
	g_notesViewType = GetPrivateProfileInt("NOTES_HELP_VIEW", "Type", 0, g_SNMIniFn.Get());
	g_locked = (GetPrivateProfileInt("NOTES_HELP_VIEW", "Lock", 0, g_SNMIniFn.Get()) == 1);
	GetPrivateProfileString("NOTES_HELP_VIEW", "BigFontName", SWSDLG_TYPEFACE, g_notesBigFontName, sizeof(g_notesBigFontName), g_SNMIniFn.Get());

	// get the action help filename
	char defaultHelpFn[BUFFER_SIZE] = "";
	if (_snprintfStrict(defaultHelpFn, sizeof(defaultHelpFn), SNM_ACTION_HELP_INI_FILE, GetResourcePath()) <= 0)
		*defaultHelpFn = '\0';
	GetPrivateProfileString("NOTES_HELP_VIEW", "Action_help_file", defaultHelpFn, g_actionHelpFn, BUFFER_SIZE, g_SNMIniFn.Get());

	g_pNotesHelpWnd = new SNM_NotesHelpWnd();
	if (!g_pNotesHelpWnd || !plugin_register("projectconfig", &g_projectconfig))
		return 0;
	return 1;
}

void NotesHelpViewExit()
{
	char tmp[4] = "";
	if (_snprintfStrict(tmp, sizeof(tmp), "%d", g_notesViewType) > 0)
		WritePrivateProfileString("NOTES_HELP_VIEW", "Type", tmp, g_SNMIniFn.Get()); 
	WritePrivateProfileString("NOTES_HELP_VIEW", "Lock", g_locked?"1":"0", g_SNMIniFn.Get()); 
	WritePrivateProfileString("NOTES_HELP_VIEW", "BigFontName", g_notesBigFontName, g_SNMIniFn.Get());

	// save the action help filename
	WDL_FastString escapedStr;
	makeEscapedConfigString(g_actionHelpFn, &escapedStr);
	WritePrivateProfileString("NOTES_HELP_VIEW", "Action_help_file", escapedStr.Get(), g_SNMIniFn.Get());

	DELETE_NULL(g_pNotesHelpWnd);
}

void OpenNotesHelpView(COMMAND_T* _ct)
{
	if (g_pNotesHelpWnd)
	{
		int newType = (int)_ct->user; // -1 means toggle current type
		if (newType == -1)
			newType = g_notesViewType;

		g_pNotesHelpWnd->Show(g_notesViewType == newType /* i.e toggle */, true);
		g_pNotesHelpWnd->SetType(newType);

		if (!g_locked)
			SetFocus(GetDlgItem(g_pNotesHelpWnd->GetHWND(), IDC_EDIT));
	}
}

bool IsNotesHelpViewDisplayed(COMMAND_T* _ct) {
	return (g_pNotesHelpWnd && g_pNotesHelpWnd->IsValidWindow());
}

void ToggleNotesHelpLock(COMMAND_T*) {
	if (g_pNotesHelpWnd)
		g_pNotesHelpWnd->ToggleLock();
}

bool IsNotesHelpLocked(COMMAND_T*) {
	return g_locked;
}
