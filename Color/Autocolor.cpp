/******************************************************************************
/ Autocolor.cpp
/
/ Copyright (c) 2010 Tim Payne (SWS) / JF B�dague (S&M)
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

#include "stdafx.h"
#include "Color.h"
#include "Autocolor.h"
#include "../SnM/SnM_Actions.h"
#include "../SnM/SNM_ChunkParserPatcher.h"


#define PRI_UP_MSG		0x10000
#define PRI_DOWN_MSG	0x10001
#define TRACKTYPE_MSG	0x10100
#define COLORTYPE_MSG	0x10110
#define LOAD_ICON_MSG	0x110000
#define CLEAR_ICON_MSG	0x110001

// INI params
#define AC_ENABLE_KEY	"AutoColorEnable"
#define AI_ENABLE_KEY	"AutoIconEnable"
#define AC_COUNT_KEY	"AutoColorCount"
#define AC_ITEM_KEY		"AutoColor %d"

enum { AC_UNNAMED, AC_FOLDER, AC_CHILDREN, AC_RECEIVE, AC_ANY, AC_MASTER, NUM_TRACKTYPES };
static const char cTrackTypes[][11] = { "(unnamed)", "(folder)", "(children)", "(receive)", "(any)", "(master)" };

enum { AC_CUSTOM, AC_GRADIENT, AC_RANDOM, AC_NONE, AC_PARENT, NUM_COLORTYPES };
static const char cColorTypes[][9] = { "Custom", "Gradient", "Random", "None", "Parent" };

// Globals
static SWS_AutoColorWnd* g_pACWnd = NULL;
static WDL_PtrList<SWS_AutoColorItem> g_pACItems;
static SWSProjConfig<WDL_PtrList<SWS_AutoColorTrack> > g_pACTracks;
static bool g_bACEnabled = false;
static bool g_bAIEnabled = false;

static SWS_LVColumn g_cols[] = { {25, 0, "#" }, { 185, 1, "Filter" }, { 70, 1, "Color" }, { 200, 2, "Icon" }};

SWS_AutoColorView::SWS_AutoColorView(HWND hwndList, HWND hwndEdit)
:SWS_ListView(hwndList, hwndEdit, 4, g_cols, "AutoColorViewState", false)
{
}

void SWS_AutoColorView::GetItemText(LPARAM item, int iCol, char* str, int iStrMax)
{
	SWS_AutoColorItem* pItem = (SWS_AutoColorItem*)item;
	if (!pItem)
		return;

	switch (iCol)
	{
	case 0: // #
		_snprintf(str, iStrMax, "%d", g_pACItems.Find(pItem) + 1);
		break;
	case 1: // Filter
		lstrcpyn(str, pItem->m_str.Get(), iStrMax);
		break;
	case 2: // Color
		if (pItem->m_col < 0)
			lstrcpyn(str, cColorTypes[-pItem->m_col - 1], iStrMax);
		else
#ifdef _WIN32
			_snprintf(str, iStrMax, "0x%02x%02x%02x", pItem->m_col & 0xFF, (pItem->m_col >> 8) & 0xFF, (pItem->m_col >> 16) & 0xFF);
#else
			_snprintf(str, iStrMax, "0x%06x", pItem->m_col);
#endif
		break;
	case 3: // icon
		lstrcpyn(str, pItem->m_icon.Get(), iStrMax);
		break;
	}
}

void SWS_AutoColorView::SetItemText(LPARAM item, int iCol, const char* str)
{
	SWS_AutoColorItem* pItem = (SWS_AutoColorItem*)item;
	if (!pItem)
		return;

	switch (iCol)
	{
	case 1: // Filter
		for (int i = 0; i < g_pACItems.GetSize(); i++)
			if (strcmp(g_pACItems.Get(i)->m_str.Get(), str) == 0)
			{
				MessageBox(GetParent(m_hwndList), "Autocolor entry with that name already exists.", "Autocolor Error", MB_OK);
				return;
			}
		if (strlen(str))
			pItem->m_str.Set(str);
		break;
	case 2: // Color
		{
			int iNewCol = strtol(str, NULL, 0);
			pItem->m_col = RGB((iNewCol >> 16) & 0xFF, (iNewCol >> 8) & 0xFF, iNewCol & 0xFF);
			break;
		}
	}

	g_pACWnd->Update();
}

void SWS_AutoColorView::GetItemList(WDL_TypedBuf<LPARAM>* pBuf)
{
	pBuf->Resize(g_pACItems.GetSize());
	for (int i = 0; i < pBuf->GetSize(); i++)
		pBuf->Get()[i] = (LPARAM)g_pACItems.Get(i);
}

void SWS_AutoColorView::OnItemDblClk(LPARAM item, int iCol)
{
	SWS_AutoColorItem* pItem = (SWS_AutoColorItem*)item;
	if (pItem && iCol == 3)
		g_pACWnd->OnCommand(LOAD_ICON_MSG, item);
}

void SWS_AutoColorView::OnItemSelChanged(LPARAM item, int iState)
{
	g_pACWnd->Update();
}

SWS_AutoColorWnd::SWS_AutoColorWnd()
:SWS_DockWnd(IDD_AUTOCOLOR, "Auto color & icon", 30005, SWSGetCommandID(OpenAutoColor))
#ifndef _WIN32
	,m_bSettingColor(false)
#endif
{
	if (m_bShowAfterInit)
		Show(false, false);
}

void SWS_AutoColorWnd::Update()
{
	if (IsValidWindow())
	{
		// Set the checkbox
		CheckDlgButton(m_hwnd, IDC_ENABLED, g_bACEnabled ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(m_hwnd, IDC_CHECK2, g_bAIEnabled ? BST_CHECKED : BST_UNCHECKED);
		SetDlgItemText(m_hwnd, IDC_APPLY, (g_bACEnabled || g_bAIEnabled) ? "Force" : "Apply");

		// Redraw the owner drawn button
#ifdef _WIN32
		RedrawWindow(GetDlgItem(m_hwnd, IDC_COLOR), NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
#else
		InvalidateRect(GetDlgItem(m_hwnd, IDC_COLOR), NULL, 0);
#endif							

		if (m_pLists.GetSize())
			m_pLists.Get(0)->Update();

		AutoColorRun(false);
	}
}

void SWS_AutoColorWnd::OnInitDlg()
{
	m_resize.init_item(IDC_LIST,      0.0, 0.0, 1.0, 1.0);
	m_resize.init_item(IDC_ADD,       0.0, 1.0, 0.0, 1.0);
	m_resize.init_item(IDC_REMOVE,    0.0, 1.0, 0.0, 1.0);
	m_resize.init_item(IDC_COLOR,     0.0, 1.0, 0.0, 1.0);
	m_resize.init_item(IDC_ENABLED,   0.0, 1.0, 0.0, 1.0);
	m_resize.init_item(IDC_CHECK2,    0.0, 1.0, 0.0, 1.0);
	m_resize.init_item(IDC_APPLY,     1.0, 1.0, 1.0, 1.0);
	m_pLists.Add(new SWS_AutoColorView(GetDlgItem(m_hwnd, IDC_LIST), GetDlgItem(m_hwnd, IDC_EDIT)));
	Update();
}

void SWS_AutoColorWnd::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam)
	{
		case IDC_APPLY:
			AutoColorRun(true);
			break;
		case IDC_ADD:
			g_pACItems.Add(new SWS_AutoColorItem("(name)", -AC_NONE-1, ""));
			Update();
			break;
		case IDC_REMOVE:
		{
			int x = 0;
			SWS_AutoColorItem* item;
			while((item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(&x)))
			{
				int idx = g_pACItems.Find(item);
				if (idx >= 0)
					g_pACItems.Delete(idx);
			}
			Update();
			break;
		}
		case IDC_ENABLED:
			g_bACEnabled = !g_bACEnabled;
			Update();
			break;
		case IDC_CHECK2:
			g_bAIEnabled = !g_bAIEnabled;
			Update();
			break;
		case IDC_COLOR:
		{
			SWS_AutoColorItem* item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(NULL);
			if (item)
			{
				// Display the color picker
#ifdef _WIN32
				CHOOSECOLOR cc;
				memset(&cc, 0, sizeof(CHOOSECOLOR));
				cc.lStructSize = sizeof(CHOOSECOLOR);
				cc.hwndOwner = m_hwnd;
				UpdateCustomColors();
				cc.lpCustColors = g_custColors;
				cc.Flags = CC_FULLOPEN | CC_RGBINIT;
				cc.rgbResult = item->m_col;
				if (ChooseColor(&cc))
				{
					int x = 0;
					while ((item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(&x)))
						item->m_col = cc.rgbResult;
				}
				Update();
#else
				m_bSettingColor = true;
				ShowColorChooser(item->m_col);
				SetTimer(m_hwnd, 1, 50, NULL);
#endif
			}
			break;
		}
		case PRI_UP_MSG:
		{
			int x = 0;
			SWS_AutoColorItem* item;
			while ((item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(&x)))
			{
				int iPos = g_pACItems.Find(item);
				if (iPos <= 0)
					break;
				g_pACItems.Delete(iPos, false);
				g_pACItems.Insert(iPos-1, item);
			}
			Update();
			break;
		}
		case PRI_DOWN_MSG:
		{
			// Go in reverse order
			for (int i = m_pLists.Get(0)->GetListItemCount()-1; i >= 0; i--)
			{
				if (m_pLists.Get(0)->IsSelected(i))
				{
					SWS_AutoColorItem* item = (SWS_AutoColorItem*)m_pLists.Get(0)->GetListItem(i);
					int iPos = g_pACItems.Find(item);
					if (iPos < 0)
						break;
					g_pACItems.Delete(iPos, false);
					g_pACItems.Insert(iPos+1, item);
				}
			}
			Update();
			break;
		}
		case LOAD_ICON_MSG:
		{
			SWS_AutoColorItem* item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(NULL);
			if (item)
			{
				char filename[BUFFER_SIZE], dir[32];
				sprintf(dir,"Data%ctrack_icons",PATH_SLASH_CHAR);
				if (BrowseResourcePath("S&M - Load icon", dir, "PNG files (*.PNG)\0*.PNG\0ICO files (*.ICO)\0*.ICO\0JPEG files (*.JPG)\0*.JPG\0BMP files (*.BMP)\0*.BMP\0PCX files (*.PCX)\0*.PCX\0", 
					filename, BUFFER_SIZE))
					item->m_icon.Set(filename);
			}
			Update();
			break;
		}
		case CLEAR_ICON_MSG:
		{
			SWS_AutoColorItem* item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(NULL);
			if (item)
				item->m_icon.Set("");
			Update();
			break;
		}

		default:
			if (wParam >= TRACKTYPE_MSG && wParam < TRACKTYPE_MSG + NUM_TRACKTYPES)
			{
				SWS_AutoColorItem* item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(NULL);
				if (item)
				{
					int iType = (int)wParam - TRACKTYPE_MSG;
					for (int i = 0; i < g_pACItems.GetSize(); i++)
						if (strcmp(g_pACItems.Get(i)->m_str.Get(), cTrackTypes[iType]) == 0)
						{
							MessageBox(m_hwnd, "Autocolor entry of that type already exists.", "Autocolor Error", MB_OK);
							return;
						}

					item->m_str.Set(cTrackTypes[wParam-TRACKTYPE_MSG]);
				}
				Update();
			}
			else if (wParam >= COLORTYPE_MSG && wParam < COLORTYPE_MSG + NUM_COLORTYPES)
			{
				int x = 0;
				SWS_AutoColorItem* item;
				while ((item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(&x)))
					item->m_col = -1 - ((int)wParam - COLORTYPE_MSG);
				Update();
			}
			else
				Main_OnCommand((int)wParam, (int)lParam);
	}
}

#ifndef _WIN32
void SWS_AutoColorWnd::OnTimer()
{
	COLORREF cr;
	if (m_bSettingColor && GetChosenColor(&cr))
	{
		int x = 0;
		SWS_AutoColorItem* item;
		while ((item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(&x)))
			item->m_col = cr;

		KillTimer(m_hwnd, 1);
		Update();
	}
}

void SWS_AutoColorWnd::OnDestroy()
{
	if (m_bSettingColor)
	{
		HideColorChooser();
		KillTimer(m_hwnd, 1);
	}
}
#endif

int SWS_AutoColorWnd::OnUnhandledMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_DRAWITEM)
	{
		LPDRAWITEMSTRUCT pDI = (LPDRAWITEMSTRUCT)lParam;
		if (pDI->CtlID == IDC_COLOR)
		{
			// Dialog-box background grey for no selection, or multiple colors in selection	
			// Doesn't account for the "special colors" because they're less than zero and get trapped
			// out below anyway.
			int col = -1;
			int x = 0;
			SWS_AutoColorItem* item;
			while ((item = (SWS_AutoColorItem*)m_pLists.Get(0)->EnumSelected(&x)))
			{
				if (col < 0)
					col = item->m_col;
				else if (col != item->m_col)
				{
					col = -1;
					break;
				}
			}

			if (col < 0)
				col = GetSysColor(COLOR_3DFACE);

			HBRUSH hb = CreateSolidBrush(col);
			FillRect(pDI->hDC, &pDI->rcItem, hb);
			DeleteObject(hb);
			return 1;
		}
	}

	return 0;
}

HMENU SWS_AutoColorWnd::OnContextMenu(int x, int y)
{
	int iCol;
	LPARAM item = m_pLists.Get(0)->GetHitItem(x, y, &iCol);
	HMENU hMenu = CreatePopupMenu();

	if (item && iCol == 1)
	{
		for (int i = 0; i < NUM_TRACKTYPES; i++)
			AddToMenu(hMenu, cTrackTypes[i], TRACKTYPE_MSG + i);
		AddToMenu(hMenu, SWS_SEPARATOR, 0);
	}
	else if (item && iCol == 2)
	{
		for (int i = 0; i < NUM_COLORTYPES; i++)
			AddToMenu(hMenu, cColorTypes[i], COLORTYPE_MSG + i);
		AddToMenu(hMenu, SWS_SEPARATOR, 0);
	}
	else if (item && iCol == 3)
	{
		AddToMenu(hMenu, "Load icon...", LOAD_ICON_MSG);
		AddToMenu(hMenu, "Clear icon", CLEAR_ICON_MSG);
		AddToMenu(hMenu, SWS_SEPARATOR, 0);
	}

	if (item)
	{
		AddToMenu(hMenu, "Up in priority", PRI_UP_MSG);
		AddToMenu(hMenu, "Down in priority", PRI_DOWN_MSG);
	}

	AddToMenu(hMenu, "Color management...", SWSGetCommandID(ShowColorDialog));

	return hMenu;
}

int SWS_AutoColorWnd::OnKey(MSG* msg, int iKeyState)
{
	if (msg->message == WM_KEYDOWN && msg->wParam == VK_DELETE && !iKeyState)
	{
		OnCommand(IDC_REMOVE, 0);
		return 1;
	}
	return 0;
} 


void OpenAutoColor(COMMAND_T*)
{
	g_pACWnd->Show(true, true);
}

void ApplyColorRule(SWS_AutoColorItem* rule, bool _autoIcon)
{
	int iCount = 0;
	WDL_PtrList<void> gradientTracks;

	if (rule->m_col == -AC_CUSTOM-1)
		UpdateCustomColors();

	// Check all tracks for matching strings/properties
	MediaTrack* temp;
	for (int i = 0; i <= GetNumTracks(); i++)
	{
		MediaTrack* tr = CSurf_TrackFromID(i, false);
		int iCurColor = *(int*)GetSetMediaTrackInfo(tr, "I_CUSTOMCOLOR", NULL);
		bool bAutocolored = false;
		bool bFound = false;
		SWS_AutoColorTrack* pACTrack = NULL;
		for (int j = 0; j < g_pACTracks.Get()->GetSize(); j++)
		{
			pACTrack = g_pACTracks.Get()->Get(j);
			if (pACTrack->m_pTr == tr)
			{
				bFound = true;
				if (!pACTrack->m_bColored && iCurColor == pACTrack->m_col)
				{
					bAutocolored = true;
					break;
				}
			}
		}
		
		// New tracks are by default autocolored
		if (!bFound)
			bAutocolored = true;
		
		if (bAutocolored)
		{
			bool bMatch = false;

			if (i) // ignore master for most things
			{
				// Check "special" rules first:
				if (strcmp(rule->m_str.Get(), cTrackTypes[AC_FOLDER]) == 0)
				{
					int iType;
					GetFolderDepth(tr, &iType, &temp);
					if (iType == 1)
						bMatch = true;
				}
				else if (strcmp(rule->m_str.Get(), cTrackTypes[AC_CHILDREN]) == 0)
				{
					temp = CSurf_TrackFromID(0, false); // JFB fix: 'temp' could be out of sync 
					if (GetFolderDepth(tr, NULL, &temp) >= 1)
						bMatch = true;
				}
				else if (strcmp(rule->m_str.Get(), cTrackTypes[AC_RECEIVE]) == 0)
				{
					if (GetSetTrackSendInfo(tr, -1, 0, "P_SRCTRACK", NULL))
						bMatch = true;
				}
				else if (strcmp(rule->m_str.Get(), cTrackTypes[AC_UNNAMED]) == 0)
				{
					char* cName = (char*)GetSetMediaTrackInfo(tr, "P_NAME", NULL);
					if (!cName || !cName[0])
						bMatch = true;
				}
				else if (strcmp(rule->m_str.Get(), cTrackTypes[AC_ANY]) == 0)
				{
					bMatch = true;
				}
				else // Check for name match
				{
					char* cName = (char*)GetSetMediaTrackInfo(tr, "P_NAME", NULL);
					if (cName && stristr(cName, rule->m_str.Get()))
						bMatch = true;
				}
			}
			else if (strcmp(rule->m_str.Get(), cTrackTypes[AC_MASTER]) == 0)
			{	// Check master rule
				bMatch = true;
			}

			if (bMatch)
			{
				int newCol = iCurColor;

				if (rule->m_col == -AC_RANDOM-1)
				{
					// Only randomize once
					if (!(iCurColor & 0x1000000))
						newCol = RGB(rand() % 256, rand() % 256, rand() % 256) | 0x1000000;
				}
				else if (rule->m_col == -AC_CUSTOM-1)
				{
					if (!AllBlack())
						while(!(newCol = g_custColors[iCount++ % 16]));
					newCol |= 0x1000000;							
				}
				else if (rule->m_col == -AC_GRADIENT-1)
					gradientTracks.Add(tr);
				else if (rule->m_col == -AC_NONE-1)
					newCol = 0;
				else if (rule->m_col == -AC_PARENT-1)
				{
					MediaTrack* parent = (MediaTrack*)GetSetMediaTrackInfo(tr, "P_PARTRACK", NULL);
					if (parent)
					{
						int pcol = *(int*)GetSetMediaTrackInfo(parent, "I_CUSTOMCOLOR", NULL);
						if (pcol & 0x1000000) // Only color like parent if the parent has color (maybe not?)
							newCol = pcol;
					}
				}
				else
					newCol = rule->m_col | 0x1000000;

				if (newCol != iCurColor)
					GetSetMediaTrackInfo(tr, "I_CUSTOMCOLOR", &newCol);
//JFB --->
				SNM_ChunkParserPatcher p(tr); // nothing done yet
				if (_autoIcon)
				{
					char pIconLine[BUFFER_SIZE] = "";
					p.Parse(SNM_GET_CHUNK_CHAR, 1, "TRACK", "TRACKIMGFN", 2, 0, 1, pIconLine, NULL, "TRACKID");
					if (strcmp(pIconLine, rule->m_icon.Get()))
					{
						if (rule->m_icon.GetLength())
							sprintf(pIconLine, "TRACKIMGFN \"%s\"\n", rule->m_icon.Get());	
						else
							*pIconLine = 0;
						p.InsertOrReplaceLine(-1, "TRACK", 1, 0, "FX", "TRACKIMGFN", pIconLine, "TRACKID");
					}
				}
//JFB <---
				if (bFound)
				{
					pACTrack->m_col = newCol;
					pACTrack->m_bColored = true;
				}
				else
					g_pACTracks.Get()->Add(new SWS_AutoColorTrack(tr, newCol));
			}
		}
	}
	
	// Handle gradients
	for (int i = 0; i < gradientTracks.GetSize(); i++)
	{
		int newCol = g_crGradStart | 0x1000000;
		if (i && gradientTracks.GetSize() > 1)
			newCol = CalcGradient(g_crGradStart, g_crGradEnd, (double)i / (gradientTracks.GetSize()-1)) | 0x1000000;
		for (int j = 0; j < g_pACTracks.Get()->GetSize(); j++)
			if (g_pACTracks.Get()->Get(j)->m_pTr == (MediaTrack*)gradientTracks.Get(i))
			{
				g_pACTracks.Get()->Get(j)->m_col = newCol;
				break;
			}
		GetSetMediaTrackInfo((MediaTrack*)gradientTracks.Get(i), "I_CUSTOMCOLOR", &newCol);
	}
}

// Here's the meat and potatoes, apply the colors!
void AutoColorRun(bool bForce)
{
	if (!g_bACEnabled && !g_bAIEnabled && !bForce)
		return;

	// If forcing, start over with the saved track list
	if (bForce)
		g_pACTracks.Get()->Empty(true);
	else
		// Remove non-existant tracks from the autocolortracklist
		for (int i = 0; i < g_pACTracks.Get()->GetSize(); i++)
			if (CSurf_TrackToID(g_pACTracks.Get()->Get(i)->m_pTr, false) < 0)
			{
				g_pACTracks.Get()->Delete(i, true);
				i--;
			}

	// Clear the "colored" bit
	for (int i = 0; i < g_pACTracks.Get()->GetSize(); i++)
		g_pACTracks.Get()->Get(i)->m_bColored = false;

	// Apply the rules
	for (int i = 0; i < g_pACItems.GetSize(); i++)
		ApplyColorRule(g_pACItems.Get(i), g_bAIEnabled || bForce);

	if (bForce)
		Undo_OnStateChangeEx("Apply SWS autocolor", UNDO_STATE_TRACKCFG | UNDO_STATE_MISCCFG, -1);
}


void EnableAutoColor(COMMAND_T*)
{
	g_bACEnabled = !g_bACEnabled;
	g_pACWnd->Update();
}

void EnableAutoIcon(COMMAND_T*)
{
	g_bAIEnabled = !g_bAIEnabled;
	g_pACWnd->Update();
}

void ApplyAutoColor(COMMAND_T*)
{
	AutoColorRun(true);
}

static bool IsAutoColorOpen(COMMAND_T*)		{ return g_pACWnd->IsValidWindow(); }
static bool IsAutoColorEnabled(COMMAND_T*)	{ return g_bACEnabled; }
static bool IsAutoIconEnabled(COMMAND_T*)	{ return g_bAIEnabled; }

static bool ProcessExtensionLine(const char *line, ProjectStateContext *ctx, bool isUndo, struct project_config_extension_t *reg)
{
	LineParser lp(false);
	if (lp.parse(line) || lp.getnumtokens() < 1)
		return false;
	if (strcmp(lp.gettoken_str(0), "<SWSAUTOCOLOR") == 0)
	{
		char linebuf[4096];
		while(true)
		{
			if (!ctx->GetLine(linebuf,sizeof(linebuf)) && !lp.parse(linebuf))
			{
				if (lp.gettoken_str(0)[0] == '>')
					break;

				GUID g;		
				stringToGuid(lp.gettoken_str(0), &g);
				MediaTrack* tr = GuidToTrack(&g);
				if (tr)
					g_pACTracks.Get()->Add(new SWS_AutoColorTrack(tr, lp.gettoken_int(1)));
			}
			else
				break;
		}
		return true;
	}
	return false;
}

static void SaveExtensionConfig(ProjectStateContext *ctx, bool isUndo, struct project_config_extension_t *reg)
{
	// first delete unused tracks
	for (int i = 0; i < g_pACTracks.Get()->GetSize(); i++)
		if (CSurf_TrackToID(g_pACTracks.Get()->Get(i)->m_pTr, false) < 0)
		{
			g_pACTracks.Get()->Delete(i, true);
			i--;
		}

	if (g_pACTracks.Get()->GetSize())
	{
		ctx->AddLine("<SWSAUTOCOLOR");
		char str[128];
		for (int i = 0; i < g_pACTracks.Get()->GetSize(); i++)
		{
			GUID g;
			if (CSurf_TrackToID(g_pACTracks.Get()->Get(i)->m_pTr, false))
				g = *(GUID*)GetSetMediaTrackInfo(g_pACTracks.Get()->Get(i)->m_pTr, "GUID", NULL);
			else
				g = GUID_NULL;
			guidToString(&g, str);
			sprintf(str+strlen(str), " %d", g_pACTracks.Get()->Get(i)->m_col);
			ctx->AddLine(str);
		}
		ctx->AddLine(">");
	}
}

static void BeginLoadProjectState(bool isUndo, struct project_config_extension_t *reg)
{
	g_pACTracks.Cleanup();
	g_pACTracks.Get()->Empty(true);
}

static project_config_extension_t g_projectconfig = { ProcessExtensionLine, SaveExtensionConfig, BeginLoadProjectState, NULL };

static COMMAND_T g_commandTable[] = 
{
	{ { DEFACCEL, "SWS: Open auto color/icon window" },		"SWSAUTOCOLOR_OPEN",	OpenAutoColor,		"SWS Auto color/icon",			0, IsAutoColorOpen },
	{ { DEFACCEL, "SWS: Toggle auto coloring enable" },	"SWSAUTOCOLOR_ENABLE",	EnableAutoColor,	"Enable SWS auto coloring", 0, IsAutoColorEnabled },
	{ { DEFACCEL, "SWS/S&M: Toggle auto icon enable" },	"S&MAUTOICON_ENABLE",	EnableAutoIcon,	"Enable SWS/S&M auto icon", 0, IsAutoIconEnabled },
	{ { DEFACCEL, "SWS: Apply auto coloring" },			"SWSAUTOCOLOR_APPLY",	ApplyAutoColor,	},
	{ {}, LAST_COMMAND, }, // Denote end of table
};

static void menuhook(const char* menustr, HMENU hMenu, int flag)
{
	if (strcmp(menustr, "Main view") == 0 && flag == 0)
		AddToMenu(hMenu, g_commandTable[0].menuText, g_commandTable[0].accel.accel.cmd, 40075);
	else if (flag == 0 && strcmp(menustr, "Main options") == 0)
		AddToMenu(hMenu, g_commandTable[1].menuText, g_commandTable[1].accel.accel.cmd, 40745);
}

int AutoColorInit()
{
	if (!plugin_register("projectconfig",&g_projectconfig))
		return 0;

	SWSRegisterCommands(g_commandTable);

	if (!plugin_register("hookcustommenu", (void*)menuhook))
		return 0;

	// Restore state
	char str[BUFFER_SIZE];
	g_bACEnabled = GetPrivateProfileInt(SWS_INI, AC_ENABLE_KEY, 0, get_ini_file()) ? true : false;
	g_bAIEnabled = GetPrivateProfileInt(SWS_INI, AI_ENABLE_KEY, 0, get_ini_file()) ? true : false;

	int iCount = GetPrivateProfileInt(SWS_INI, AC_COUNT_KEY, 0, get_ini_file());
	for (int i = 0; i < iCount; i++)
	{
		char key[32];
		_snprintf(key, 32, AC_ITEM_KEY, i+1);
		GetPrivateProfileString(SWS_INI, key, "\"\" 0 \'\'", str, BUFFER_SIZE, get_ini_file());
		LineParser lp(false);
		if (!lp.parse(str))
			g_pACItems.Add(new SWS_AutoColorItem(lp.gettoken_str(0), lp.gettoken_int(1), lp.gettoken_str(2)));
	}	

	g_pACWnd = new SWS_AutoColorWnd();

	return 1;
}

void AutoColorExit()
{
	// Save state
	char str[BUFFER_SIZE];
	sprintf(str, "%d", g_bACEnabled ? 1 : 0);
	WritePrivateProfileString(SWS_INI, AC_ENABLE_KEY, str, get_ini_file());
	sprintf(str, "%d", g_bAIEnabled ? 1 : 0);
	WritePrivateProfileString(SWS_INI, AI_ENABLE_KEY, str, get_ini_file());
	sprintf(str, "%d", g_pACItems.GetSize());
	WritePrivateProfileString(SWS_INI, AC_COUNT_KEY, str, get_ini_file());
	for (int i = 0; i < g_pACItems.GetSize(); i++)
	{
		char key[32];
		_snprintf(key, 32, AC_ITEM_KEY, i+1);
		_snprintf(str, BUFFER_SIZE, "\"%s\" %d \'%s\'", g_pACItems.Get(i)->m_str.Get(), g_pACItems.Get(i)->m_col, g_pACItems.Get(i)->m_icon.Get());
		WritePrivateProfileString(SWS_INI, key, str, get_ini_file());
	}	

	delete g_pACWnd;
}
