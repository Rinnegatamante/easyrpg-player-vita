﻿/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

// Headers
#include <string>

#include "window_keyboard.h"
#include "game_system.h"
#include "input.h"
#include "bitmap.h"
#include "font.h"

/** Interprets char literals as utf-8 */
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

const char* const Window_Keyboard::TO_SYMBOL = "Symbol";
const char* const Window_Keyboard::TO_LETTER = "Letter";
const char* const Window_Keyboard::DONE = "Done";
const char* const Window_Keyboard::SPACE = "SPACE";

const char* const Window_Keyboard::TO_KATAKANA = "<カナ>";
const char* const Window_Keyboard::TO_HIRAGANA = "<かな>";
const char* const Window_Keyboard::DONE_JP = "<決定>";

/*
 * hiragana -> katakana -> letter -> symbol -> hiragana -> ...
 */

std::string Window_Keyboard::items[Window_Keyboard::MODE_END][9][10] = {
	{ // Hiragana
	{"あ","い","う","え","お","が","ぎ","ぐ","げ","ご"},
	{"か","き","く","け","こ","ざ","じ","ず","ぜ","ぞ"},
	{"さ","し","す","せ","そ","だ","ぢ","づ","で","ど"},
	{"た","ち","つ","て","と","ば","び","ぶ","べ","ぼ"},
	{"な","に","ぬ","ね","の","ぱ","ぴ","ぷ","ぺ","ぽ"},
	{"は","ひ","ふ","へ","ほ","ぁ","ぃ","ぅ","ぇ","ぉ"},
	{"ま","み","む","め","も","っ","ゃ","ゅ","ょ","ゎ"},
	{"や","ゆ","よ","わ","ん","ー","〜","・","＝","☆"},
	{"ら","り","る","れ","ろ","を",Window_Keyboard::TO_KATAKANA,"",Window_Keyboard::DONE_JP}
	},

	{ // Katakana
	{"ア","イ","ウ","エ","オ","ガ","ギ","グ","ゲ","ゴ"},
	{"カ","キ","ク","ケ","コ","ザ","ジ","ズ","ゼ","ゾ"},
	{"サ","シ","ス","セ","ソ","ダ","ヂ","ヅ","デ","ド"},
	{"タ","チ","ツ","テ","ト","バ","ビ","ブ","ベ","ボ"},
	{"ナ","ニ","ヌ","ネ","ノ","パ","ピ","プ","ペ","ポ"},
	{"ハ","ヒ","フ","ヘ","ホ","ァ","ィ","ゥ","ェ","ォ"},
	{"マ","ミ","ム","メ","モ","ッ","ャ","ュ","ョ","ヮ"},
	{"ヤ","ユ","ヨ","ワ","ン","ー","〜","・","＝","☆"},
	{"ラ","リ","ル","レ","ロ","ヲ",Window_Keyboard::TO_LETTER,"",Window_Keyboard::DONE_JP}
	},

	{ // Letter
	{"A","B","C","D","E","a","b","c","d","e"},
	{"F","G","H","I","J","f","g","h","i","j"},
	{"K","L","M","N","O","k","l","m","n","o"},
	{"P","Q","R","S","T","p","q","r","s","t"},
	{"U","V","W","X","Y","u","v","w","x","y"},
	{"Z","" ,"" ,"" ,"" ,"z",},
	{"0","1","2","3","4","5","6","7","8","9"},
	{Window_Keyboard::SPACE},
	{"","","","","","",Window_Keyboard::TO_SYMBOL,"",Window_Keyboard::DONE},
	},

	{ // Symbol
	{"$A","$B","$C","$D","$E","$a","$b","$c","$d","$e"},
	{"$F","$G","$H","$I","$J","$f","$g","$h","$i","$j"},
	{"$K","$L","$M","$N","$O","$k","$l","$m","$n","$o"},
	{"$P","$Q","$R","$S","$T","$p","$q","$r","$s","$t"},
	{"$U","$V","$W","$X","$Y","$u","$v","$w","$x","$y"},
	{"$Z",""  ,""  ,""  ,""  ,"$z"},
	{},
	{},
	{"","","","","","",Window_Keyboard::TO_HIRAGANA,"",Window_Keyboard::DONE},
	},
};

Window_Keyboard::Window_Keyboard(int ix, int iy, int iwidth, int iheight)
		: Window_Base(ix, iy, iwidth, iheight)
		, play_cursor(false)
{
	row = 0;
	col = 0;

	SetContents(Bitmap::Create(width - 16, height - 16));
	contents->SetTransparentColor(windowskin->GetTransparentColor());
	SetZ(9999);

	row_spacing = 16;
	col_spacing = (contents->GetWidth() - 2 * border_x) / col_max;

	mode = Letter;

	Refresh();
	UpdateCursorRect();
}

void Window_Keyboard::SetMode(Window_Keyboard::Mode nmode) {
	mode = nmode;
	Refresh();
	UpdateCursorRect();
}

std::string const& Window_Keyboard::GetSelected() const {
	return items[mode][row][col];
}

Rect Window_Keyboard::GetItemRect(int row, int col) const {
	std::string const& str = items[mode][row][col];
	return Rect(col * col_spacing + border_x,
				row * row_spacing + border_y,
				contents->GetFont()->GetSize(str).width + 8,
				row_spacing);
}

void Window_Keyboard::UpdateCursorRect() {
	Rect r = GetItemRect(row, col);
	r.y -= 2;
	SetCursorRect(r);
}

void Window_Keyboard::Refresh() {
	contents->Clear();

	for (int j = 0; j < row_max; j++) {
		for (int i = 0; i < col_max; i++) {
			Rect r = GetItemRect(j, i);
			contents->TextDraw(r.x + 4, r.y, Font::ColorDefault, items[mode][j][i]);
		}
	}
}

void Window_Keyboard::Update() {
	Window_Base::Update();

	if (active) {
		if (Input::IsRepeated(Input::DOWN)) {
			play_cursor = true;
			row = (row + 1) % row_max;

			if(col > 0 && GetSelected().empty() && !items[mode][row][col - 1].empty()) {
				col--;
			}
		}
		if (Input::IsRepeated(Input::UP)) {
			play_cursor = true;
			row = (row + row_max - 1) % row_max;

			if(col > 0 && GetSelected().empty() && !items[mode][row][col - 1].empty()) {
				col--;
			}
		}
		if (Input::IsRepeated(Input::RIGHT)) {
			play_cursor = true;
			col += 1;
			if (col >= col_max) {
				col = 0;
				if(mode == Letter) { row = (row + 1) % row_max; }
			}
		}
		if (Input::IsRepeated(Input::LEFT)) {
			play_cursor = true;
			col -= 1;
			if (col < 0) {
				col = col_max - 1;
				if(mode == Letter) { row = (row + row_max - 1) % row_max; }
			}
		}

	}

	if(GetSelected().empty()) {
		Update();
		return;
	}

	if(play_cursor) {
		Game_System::SePlay(Game_System::GetSystemSE(Game_System::SFX_Cursor));
		play_cursor = false;
	}
	UpdateCursorRect();
}
