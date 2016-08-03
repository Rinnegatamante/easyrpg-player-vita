/*
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
#include <cstdlib>
#include <iostream>
#include <sstream>
#include "game_interpreter.h"
#include "audio.h"
#include "game_map.h"
#include "game_event.h"
#include "game_player.h"
#include "game_temp.h"
#include "game_switches.h"
#include "game_variables.h"
#include "game_party.h"
#include "game_actors.h"
#include "game_system.h"
#include "game_message.h"
#include "game_picture.h"
#include "spriteset_map.h"
#include "sprite_character.h"
#include "scene_map.h"
#include "scene.h"
#include "graphics.h"
#include "input.h"
#include "main_data.h"
#include "output.h"
#include "player.h"
#include "util_macro.h"

Game_Interpreter::Game_Interpreter(int _depth, bool _main_flag) {
	depth = _depth;
	main_flag = _main_flag;
	index = 0;
	updating = false;
	clear_child = false;
	runned = false;

	if (depth > 100) {
		Output::Warning("Too many event calls (over 9000)");
	}

	Clear();
}

 Game_Interpreter::~Game_Interpreter() {
 }

// Clear.
void Game_Interpreter::Clear() {
	map_id = 0;						// map ID when starting up
	event_id = 0;					// event ID
	wait_count = 0;					// wait count
	waiting_battle_anim = false;
	waiting_pan_screen = false;
	triggered_by_decision_key = false;
	continuation = NULL;			// function to execute to resume command
	button_timer = 0;
	if (child_interpreter) {		// clear child interpreter for called events
		if (child_interpreter->updating)
			clear_child = true;
		else
			child_interpreter.reset();
	}
	list.clear();
}

// Is interpreter running.
bool Game_Interpreter::IsRunning() const {
	return !list.empty();
}

// Setup.
void Game_Interpreter::Setup(
	const std::vector<RPG::EventCommand>& _list,
	int _event_id,
	bool started_by_decision_key,
	int dbg_x, int dbg_y
) {
	Clear();

	map_id = Game_Map::GetMapId();
	event_id = _event_id;
	list = _list;
	triggered_by_decision_key = started_by_decision_key;

	debug_x = dbg_x;
	debug_y = dbg_y;

	index = 0;

	CancelMenuCall();

	if (main_flag && depth == 0)
		Game_Message::SetFaceName("");
}

void Game_Interpreter::CancelMenuCall() {
	// TODO
}

void Game_Interpreter::SetupWait(int duration) {
	if (duration == 0) {
		// 0.0 waits 1 frame
		wait_count = 1;
	} else {
		wait_count = duration * DEFAULT_FPS / 10;
	}
}

void Game_Interpreter::SetContinuation(Game_Interpreter::ContinuationFunction func) {
	continuation = func;
}

bool Game_Interpreter::HasRunned() const {
	return runned;
}

// Update
void Game_Interpreter::Update() {
	updating = true;
	runned = false;
	// 10000 based on: https://gist.github.com/4406621
	for (loop_count = 0; loop_count < 10000; ++loop_count) {
		/* If map is different than event startup time
		set event_id to 0 */
		if (Game_Map::GetMapId() != map_id) {
			event_id = 0;
		}

		/* If there's any active child interpreter, update it */
		if (child_interpreter) {

			child_interpreter->Update();

			if (!child_interpreter->IsRunning() || clear_child) {
				child_interpreter.reset();
				clear_child = false;
			}

			// If child interpreter still exists
			if (child_interpreter) {
				break;
			}
		}

		if (main_flag) {
			if (Main_Data::game_player->IsBoardingOrUnboarding())
				break;

			if (Game_Message::message_waiting)
				break;
		} else {
			if ((Game_Message::visible || Game_Message::message_waiting) && Game_Message::owner_id == event_id)
				break;
		}

		if (wait_count > 0) {
			wait_count--;
			break;
		}

		if (Game_Temp::forcing_battler != NULL) {
			break;
		}

		if (Game_Temp::transition_processing) {
			break;
		}

		if ((Game_Temp::battle_calling && !Game_Temp::battle_running) ||
			Game_Temp::shop_calling ||
			Game_Temp::name_calling ||
			Game_Temp::menu_calling ||
			Game_Temp::save_calling ||
			Game_Temp::load_calling ||
			Game_Temp::to_title ||
			Game_Temp::gameover) {

			break;
		}

		if (continuation) {
			bool result;
			if (index >= list.size()) {
				result = (this->*continuation)(RPG::EventCommand());
			} else {
				result = (this->*continuation)(list[index]);
			}

			if (result)
				continue;
			else
				break;
		}

		if (!Main_Data::game_player->IsTeleporting() && Game_Map::GetNeedRefresh()) {
			Game_Map::Refresh();
		}

		if (list.empty()) {
			break;
		}

		runned = true;
		if (!ExecuteCommand()) {
			break;
		}

		// FIXME?
		// After calling SkipTo this index++ will skip execution of e.g. END.
		// This causes a different timing because loop_count reaches 10000
		// faster then Player does.
		// No idea if any game depends on this special case.
		index++;
	} // for

	if (loop_count > 9999) {
		// Executed Events Count exceeded (10000)
		Output::Debug("Event %d exceeded execution limit", event_id);
	}

	updating = false;
}

// Setup Starting Event
void Game_Interpreter::SetupStartingEvent(Game_Event* ev) {
	Setup(ev->GetList(), ev->GetId(), ev->WasStartedByDecisionKey(), ev->GetX(), ev->GetY());
	ev->ClearStarting();
}

void Game_Interpreter::SetupStartingEvent(Game_CommonEvent* ev) {
	Setup(ev->GetList(), 0, false, ev->GetIndex(), -2);
}

void Game_Interpreter::CheckGameOver() {
	if (!Main_Data::game_party->IsAnyActive()) {
		// Empty party is allowed
		Game_Temp::gameover = Main_Data::game_party->GetBattlerCount() > 0;
	}
}

// Skip to command.
bool Game_Interpreter::SkipTo(int code, int code2, int min_indent, int max_indent, bool otherwise_end) {
	if (code2 < 0)
		code2 = code;
	if (min_indent < 0)
		min_indent = list[index].indent;
	if (max_indent < 0)
		max_indent = list[index].indent;

	int idx;
	for (idx = index; (size_t) idx < list.size(); idx++) {
		if (list[idx].indent < min_indent)
			return false;
		if (list[idx].indent > max_indent)
			continue;
		if (list[idx].code != code &&
			list[idx].code != code2)
			continue;
		index = idx;
		return true;
	}

	if (otherwise_end)
		index = idx;

	return true;
}

// Execute Command.
bool Game_Interpreter::ExecuteCommand() {
	RPG::EventCommand const& com = list[index];

	switch (com.code) {
		case Cmd::ShowMessage:
			return CommandShowMessage(com);
		case Cmd::ChangeFaceGraphic:
			return CommandChangeFaceGraphic(com);
		case Cmd::ShowChoice:
			return CommandShowChoices(com);
		case Cmd::ShowChoiceOption:
			return SkipTo(Cmd::ShowChoiceEnd);
		case Cmd::ShowChoiceEnd:
			return true;
		case Cmd::InputNumber:
			return CommandInputNumber(com);
		case Cmd::ControlSwitches:
			return CommandControlSwitches(com);
		case Cmd::ControlVars:
			return CommandControlVariables(com);
		case Cmd::ChangeGold:
			return CommandChangeGold(com);
		case Cmd::ChangeItems:
			return CommandChangeItems(com);
		case Cmd::ChangePartyMembers:
			return CommandChangePartyMember(com);
		case Cmd::ChangeLevel:
			return CommandChangeLevel(com);
		case Cmd::ChangeSkills:
			return CommandChangeSkills(com);
		case Cmd::ChangeEquipment:
			return CommandChangeEquipment(com);
		case Cmd::ChangeHP:
			return CommandChangeHP(com);
		case Cmd::ChangeSP:
			return CommandChangeSP(com);
		case Cmd::ChangeCondition:
			return CommandChangeCondition(com);
		case Cmd::FullHeal:
			return CommandFullHeal(com);
		case Cmd::TintScreen:
			return CommandTintScreen(com);
		case Cmd::FlashScreen:
			return CommandFlashScreen(com);
		case Cmd::ShakeScreen:
			return CommandShakeScreen(com);
		case Cmd::Wait:
			return CommandWait(com);
		case Cmd::PlayBGM:
			return CommandPlayBGM(com);
		case Cmd::FadeOutBGM:
			return CommandFadeOutBGM(com);
		case Cmd::PlaySound:
			return CommandPlaySound(com);
		case Cmd::EndEventProcessing:
			return CommandEndEventProcessing(com);
		case Cmd::Comment:
		case Cmd::Comment_2:
			return true;
		case Cmd::GameOver:
			return CommandGameOver(com);
		default:
			return true;
	}
}

bool Game_Interpreter::CommandWait(RPG::EventCommand const& com) { // code 11410
	if (com.parameters.size() <= 1 ||
		(com.parameters.size() > 1 && com.parameters[1] == 0)) {
		SetupWait(com.parameters[0]);
		return true;
	} else {
		return Input::IsAnyTriggered();
	}
}

bool Game_Interpreter::CommandEnd() { // code 10
	if (main_flag && depth == 0) {
		Game_Message::SetFaceName("");
	}

	// FIXME: Hangs in some cases when Autostart events start
	//if (main_flag) {
	//	Game_Message::FullClear();
	//}

	list.clear();

	if (main_flag && depth == 0 && event_id > 0) {
		Game_Event* evnt = Game_Map::GetEvent(event_id);
		if (evnt)
			evnt->StopTalkToHero();
	}

	return true;
}

// Helper function
std::vector<std::string> Game_Interpreter::GetChoices() {
	// Let's find the choices
	int current_indent = list[index + 1].indent;
	std::vector<std::string> s_choices;
	for (unsigned index_temp = index + 1; index_temp < list.size(); ++index_temp) {
		if (list[index_temp].indent != current_indent) {
			continue;
		}

		if (list[index_temp].code == Cmd::ShowChoiceOption) {
			// Choice found
			s_choices.push_back(list[index_temp].string);
		}

		if (list[index_temp].code == Cmd::ShowChoiceEnd) {
			// End of choices found
			if (s_choices.size() > 1 && s_choices.back().empty()) {
				// Remove cancel branch
				s_choices.pop_back();
			}
			break;
		}
	}
	return s_choices;
}

// Command Show Message
bool Game_Interpreter::CommandShowMessage(RPG::EventCommand const& com) { // code 10110
	// If there's a text already, return immediately
	if (Game_Message::message_waiting)
		return false;

	// Parallel interpreters must wait until the message window is closed
	if (!main_flag && Game_Message::visible)
		return false;

	unsigned int line_count = 0;

	Game_Message::message_waiting = true;
	Game_Message::owner_id = event_id;

	// Set first line
	Game_Message::texts.push_back(com.string);
	line_count++;

	for (; index + 1 < list.size(); index++) {
		// If next event command is the following parts of the message
		if (list[index+1].code == Cmd::ShowMessage_2) {
			// Add second (another) line
			line_count++;
			Game_Message::texts.push_back(list[index+1].string);
		} else {
			// If next event command is show choices
			if (list[index+1].code == Cmd::ShowChoice) {
				std::vector<std::string> s_choices = GetChoices();
				// If choices fit on screen
				if (s_choices.size() <= (4 - line_count)) {
					index++;
					Game_Message::choice_start = line_count;
					Game_Message::choice_cancel_type = list[index].parameters[0];
					SetupChoices(s_choices);
				}
			} else if (list[index+1].code == Cmd::InputNumber) {
				// If next event command is input number
				// If input number fits on screen
				if (line_count < 4) {
					index++;
					Game_Message::num_input_start = line_count;
					Game_Message::num_input_digits_max = list[index].parameters[0];
					Game_Message::num_input_variable_id = list[index].parameters[1];
				}
			}

			break;
		}
	} // End for

	return true;
}

// Setup Choices
void Game_Interpreter::SetupChoices(const std::vector<std::string>& choices) {
	Game_Message::choice_start = Game_Message::texts.size();
	Game_Message::choice_max = choices.size();
	Game_Message::choice_disabled.reset();

	// Set choices to message text
	unsigned int i;
	for (i = 0; i < 4 && i < choices.size(); i++) {
		Game_Message::texts.push_back(choices[i]);
	}

	SetContinuation(&Game_Interpreter::ContinuationChoices);
}

bool Game_Interpreter::ContinuationChoices(RPG::EventCommand const& com) {
	continuation = NULL;
	int indent = com.indent;
	for (;;) {
		if (!SkipTo(Cmd::ShowChoiceOption, Cmd::ShowChoiceEnd, indent, indent))
			return false;
		int which = list[index].parameters[0];
		index++;
		if (which > Game_Message::choice_result)
			return false;
		if (which < Game_Message::choice_result)
			continue;
		break;
	}

	return true;
}

// Command Show choices
bool Game_Interpreter::CommandShowChoices(RPG::EventCommand const& com) { // code 10140
	if (!Game_Message::texts.empty()) {
		return false;
	}

	Game_Message::message_waiting = true;
	Game_Message::owner_id = event_id;

	// Choices setup
	std::vector<std::string> choices = GetChoices();
	Game_Message::choice_cancel_type = com.parameters[0];
	SetupChoices(choices);

	return true;
}

// Command control switches
bool Game_Interpreter::CommandControlSwitches(RPG::EventCommand const& com) { // code 10210
	int i;
	switch (com.parameters[0]) {
		case 0:
		case 1:
			// Single and switch range
			for (i = com.parameters[1]; i <= com.parameters[2]; i++) {
				if (com.parameters[3] != 2) {
					Game_Switches[i] = com.parameters[3] == 0;
				} else {
					Game_Switches[i] = !Game_Switches[i];
				}
			}
			break;
		case 2:
			// Switch from variable
			if (com.parameters[3] != 2) {
				Game_Switches[Game_Variables[com.parameters[1]]] = com.parameters[3] == 0;
			} else {
				Game_Switches[Game_Variables[com.parameters[1]]] = !Game_Switches[Game_Variables[com.parameters[1]]];
			}
			break;
		default:
			return false;
	}
	Game_Map::SetNeedRefresh(Game_Map::Refresh_All);
	return true;
}

// Command control vars
bool Game_Interpreter::CommandControlVariables(RPG::EventCommand const& com) { // code 10220
	int i, value = 0;
	Game_Actor* actor;
	Game_Character* character;

	switch (com.parameters[4]) {
		case 0:
			// Constant
			value = com.parameters[5];
			break;
		case 1:
			// Var A ops B
			value = Game_Variables[com.parameters[5]];
			break;
		case 2:
			// Number of var A ops B
			value = Game_Variables[Game_Variables[com.parameters[5]]];
			break;
		case 3:
			// Random between range
			int a, b;
			a = max(com.parameters[5], com.parameters[6]);
			b = min(com.parameters[5], com.parameters[6]);
			value = rand() % (a-b+1)+b;
			break;
		case 4:
			// Items
			switch (com.parameters[6]) {
				case 0:
					// Number of items posessed
					value = Main_Data::game_party->GetItemCount(com.parameters[5]);
					break;
				case 1:
					// How often the item is equipped
					value = Main_Data::game_party->GetItemCount(com.parameters[5], true);
					break;
			}
			break;
		case 5:
			// Hero
			actor = Game_Actors::GetActor(com.parameters[5]);
			if (actor != NULL) {
				switch (com.parameters[6]) {
					case 0:
						// Level
						value = actor->GetLevel();
						break;
					case 1:
						// Experience
						value = actor->GetExp();
						break;
					case 2:
						// Current HP
						value = actor->GetHp();
						break;
					case 3:
						// Current MP
						value = actor->GetSp();
						break;
					case 4:
						// Max HP
						value = actor->GetMaxHp();
						break;
					case 5:
						// Max MP
						value = actor->GetMaxSp();
						break;
					case 6:
						// Attack
						value = actor->GetAtk();
						break;
					case 7:
						// Defense
						value = actor->GetDef();
						break;
					case 8:
						// Intelligence
						value = actor->GetSpi();
						break;
					case 9:
						// Agility
						value = actor->GetAgi();
						break;
					case 10:
						// Weapon ID
						value = actor->GetWeaponId();
						break;
					case 11:
						// Shield ID
						value = actor->GetShieldId();
						break;
					case 12:
						// Armor ID
						value = actor->GetArmorId();
						break;
					case 13:
						// Helmet ID
						value = actor->GetHelmetId();
						break;
					case 14:
						// Accesory ID
						value = actor->GetAccessoryId();
						break;
				}
			}
			break;
		case 6:
			// Characters
			character = GetCharacter(com.parameters[5]);
			if (character != NULL) {
				switch (com.parameters[6]) {
					case 0:
						// Map ID
						value = character->GetMapId();
						break;
					case 1:
						// X Coordinate
						value = character->GetX();
						break;
					case 2:
						// Y Coordinate
						value = character->GetY();
						break;
					case 3:
						// Orientation
						int dir;
						dir = character->GetSpriteDirection();
						value = dir == 0 ? 8 :
								dir == 1 ? 6 :
								dir == 2 ? 2 : 4;
						break;
					case 4:
						// Screen X
						value = character->GetScreenX();
						break;
					case 5:
						// Screen Y
						value = character->GetScreenY();
				}
			}
			break;
		case 7:
			// More
			switch (com.parameters[5]) {
				case 0:
					// Gold
					value = Main_Data::game_party->GetGold();
					break;
				case 1:
					value = Main_Data::game_party->GetTimer(Main_Data::game_party->Timer1);
					break;
				case 2:
					// Number of heroes in party
					value = Main_Data::game_party->GetActors().size();
					break;
				case 3:
					// Number of saves
					value = Game_System::GetSaveCount();
					break;
				case 4:
					// Number of battles
					value = Main_Data::game_party->GetBattleCount();
					break;
				case 5:
					// Number of wins
					value = Main_Data::game_party->GetWinCount();
					break;
				case 6:
					// Number of defeats
					value = Main_Data::game_party->GetDefeatCount();
					break;
				case 7:
					// Number of escapes (aka run away)
					value = Main_Data::game_party->GetRunCount();
					break;
				case 8:
					// MIDI play position
					value = Audio().BGM_GetTicks();
					break;
				case 9:
					value = Main_Data::game_party->GetTimer(Main_Data::game_party->Timer2);
					break;
			}
			break;
		default:
			;
	}

	switch (com.parameters[0]) {
		case 0:
		case 1:
			// Single and Var range
			for (i = com.parameters[1]; i <= com.parameters[2]; i++) {
				switch (com.parameters[3]) {
					case 0:
						// Assignement
						Game_Variables[i] = value;
						break;
					case 1:
						// Addition
						Game_Variables[i] += value;
						break;
					case 2:
						// Subtraction
						Game_Variables[i] -= value;
						break;
					case 3:
						// Multiplication
						Game_Variables[i] *= value;
						break;
					case 4:
						// Division
						if (value != 0) {
							Game_Variables[i] /= value;
						}
						break;
					case 5:
						// Module
						if (value != 0) {
							Game_Variables[i] %= value;
						} else {
							Game_Variables[i] = 0;
						}
				}
				if (Game_Variables[i] > MaxSize) {
					Game_Variables[i] = MaxSize;
				}
				if (Game_Variables[i] < MinSize) {
					Game_Variables[i] = MinSize;
				}
			}
			break;

		case 2:
			int var_index = Game_Variables[com.parameters[1]];
			switch (com.parameters[3]) {
				case 0:
					// Assignement
					Game_Variables[var_index] = value;
					break;
				case 1:
					// Addition
					Game_Variables[var_index] += value;
					break;
				case 2:
					// Subtraction
					Game_Variables[var_index] -= value;
					break;
				case 3:
					// Multiplication
					Game_Variables[var_index] *= value;
					break;
				case 4:
					// Division
					if (value != 0) {
						Game_Variables[var_index] /= value;
					}
					break;
				case 5:
					// Module
					if (value != 0) {
						Game_Variables[var_index] %= value;
					}
			}
			if (Game_Variables[var_index] > MaxSize) {
				Game_Variables[var_index] = MaxSize;
			}
			if (Game_Variables[var_index] < MinSize) {
				Game_Variables[var_index] = MinSize;
			}
	}

	Game_Map::SetNeedRefresh(Game_Map::Refresh_Map);
	return true;
}

int Game_Interpreter::OperateValue(int operation, int operand_type, int operand) {
	int value = 0;

	if (operand_type == 0) {
		value = operand;
	} else {
		value = Game_Variables[operand];
	}

	// Reverse sign of value if operation is substract
	if (operation == 1) {
		value = -value;
	}

	return value;
}

std::vector<Game_Actor*> Game_Interpreter::GetActors(int mode, int id) {
	std::vector<Game_Actor*> actors;
	Game_Actor* actor;

	switch (mode) {
	case 0:
		// Party
		actors = Main_Data::game_party->GetActors();
		break;
	case 1:
		// Hero
		actor = Game_Actors::GetActor(id);
		if (actor)
			actors.push_back(actor);
		break;
	case 2:
		// Var hero
		actor = Game_Actors::GetActor(Game_Variables[id]);
		if (actor)
			actors.push_back(actor);
		break;
	}

	return actors;
}

// Get Character.
Game_Character* Game_Interpreter::GetCharacter(int character_id) const {
	Game_Character* ch = Game_Character::GetCharacter(character_id, event_id);
	if (!ch) {
		Output::Warning("Unknown event with id %d", character_id);
	}
	return ch;
}

// Change Gold.
bool Game_Interpreter::CommandChangeGold(RPG::EventCommand const& com) { // Code 10310
	int value;
	value = OperateValue(
		com.parameters[0],
		com.parameters[1],
		com.parameters[2]
	);

	Main_Data::game_party->GainGold(value);

	// Continue
	return true;
}

// Change Items.
bool Game_Interpreter::CommandChangeItems(RPG::EventCommand const& com) { // Code 10320
	int value;
	value = OperateValue(
		com.parameters[0],
		com.parameters[3],
		com.parameters[4]
	);

	// Add item can't be used to remove an item and
	// remove item can't be used to add one
	if (com.parameters[0] == 1) {
		// Substract
		if (value > 0) {
			return true;
		}
	} else {
		// Add
		if (value < 0) {
			return true;
		}
	}

	if (com.parameters[1] == 0) {
		// Item by const number
		Main_Data::game_party->AddItem(com.parameters[2], value);
	} else {
		// Item by variable
		Main_Data::game_party->AddItem(
			Game_Variables[com.parameters[2]],
			value
		);
	}
	Game_Map::SetNeedRefresh(Game_Map::Refresh_Map);
	// Continue
	return true;
}

// Input Number.
bool Game_Interpreter::CommandInputNumber(RPG::EventCommand const& com) { // code 10150
	if (Game_Message::message_waiting) {
		return false;
	}

	Game_Message::message_waiting = true;
	Game_Message::owner_id = event_id;

	Game_Message::num_input_start = 0;
	Game_Message::num_input_variable_id = com.parameters[1];
	Game_Message::num_input_digits_max = com.parameters[0];

	// Continue
	return true;
}

// Change Face Graphic.
bool Game_Interpreter::CommandChangeFaceGraphic(RPG::EventCommand const& com) { // Code 10130
	if (Game_Message::message_waiting && Game_Message::owner_id != event_id)
		return false;

	Game_Message::SetFaceName(com.string);
	Game_Message::SetFaceIndex(com.parameters[0]);
	Game_Message::SetFaceRightPosition(com.parameters[1] != 0);
	Game_Message::SetFaceFlipped(com.parameters[2] != 0);
	return true;
}

// Change Party Member.
bool Game_Interpreter::CommandChangePartyMember(RPG::EventCommand const& com) { // Code 10330
	Game_Actor* actor;
	int id;

	if (com.parameters[1] == 0) {
		id = com.parameters[2];
	} else {
		id = Game_Variables[com.parameters[2]];
	}

	actor = Game_Actors::GetActor(id);

	if (actor != NULL) {

		if (com.parameters[0] == 0) {
			// Add members
			Main_Data::game_party->AddActor(id);

		} else {
			// Remove members
			Main_Data::game_party->RemoveActor(id);

			CheckGameOver();
		}
	}

	Game_Map::SetNeedRefresh(Game_Map::Refresh_Map);

	// Continue
	return true;
}

// Change Experience.
bool Game_Interpreter::CommandChangeLevel(RPG::EventCommand const& com) { // Code 10420
	int value = OperateValue(
		com.parameters[2],
		com.parameters[3],
		com.parameters[4]
	);

	for (const auto& actor : GetActors(com.parameters[0], com.parameters[1])) {
		actor->ChangeLevel(actor->GetLevel() + value, com.parameters[5] != 0);
	}

	return true;
}

int Game_Interpreter::ValueOrVariable(int mode, int val) {
	switch (mode) {
		case 0:
			return val;
		case 1:
			return Game_Variables[val];
		default:
			return -1;
	}
}

bool Game_Interpreter::CommandChangeSkills(RPG::EventCommand const& com) { // Code 10440
	bool remove = com.parameters[2] != 0;
	int skill_id = ValueOrVariable(com.parameters[3], com.parameters[4]);

	for (const auto& actor : GetActors(com.parameters[0], com.parameters[1])) {
		if (remove)
			actor->UnlearnSkill(skill_id);
		else
			actor->LearnSkill(skill_id);
	}

	return true;
}

bool Game_Interpreter::CommandChangeEquipment(RPG::EventCommand const& com) { // Code 10450
	int item_id;
	int type;
	int slot;

	switch (com.parameters[2]) {
		case 0:
			item_id = ValueOrVariable(com.parameters[3],
									  com.parameters[4]);
			type = Data::items[item_id - 1].type;
			switch (type) {
				case RPG::Item::Type_weapon:
				case RPG::Item::Type_shield:
				case RPG::Item::Type_armor:
				case RPG::Item::Type_helmet:
				case RPG::Item::Type_accessory:
					slot = type - 1;
					break;
				default:
					return true;
			}
			break;
		case 1:
			item_id = 0;
			slot = com.parameters[3];
			break;
		default:
			return false;
	}

	for (const auto& actor : GetActors(com.parameters[0], com.parameters[1])) {
		actor->ChangeEquipment(slot, item_id);
	}

	return true;
}

bool Game_Interpreter::CommandChangeHP(RPG::EventCommand const& com) { // Code 10460
	bool remove = com.parameters[2] != 0;
	int amount = ValueOrVariable(com.parameters[3],
								 com.parameters[4]);
	bool lethal = com.parameters[5] != 0;

	if (remove)
		amount = -amount;

	for (const auto& actor : GetActors(com.parameters[0], com.parameters[1])) {
		int hp = actor->GetHp() + amount;
		if (!lethal && hp <= 0) {
			amount += hp * (-1) + 1;
		}
		actor->ChangeHp(amount);
	}

	if (lethal) {
		CheckGameOver();
	}

	return true;
}

bool Game_Interpreter::CommandChangeSP(RPG::EventCommand const& com) { // Code 10470
	bool remove = com.parameters[2] != 0;
	int amount = ValueOrVariable(com.parameters[3], com.parameters[4]);

	if (remove)
		amount = -amount;

	for (const auto& actor : GetActors(com.parameters[0], com.parameters[1])) {
		int sp = actor->GetSp() + amount;
		if (sp < 0)
			sp = 0;
		actor->SetSp(sp);
	}

	return true;
}

bool Game_Interpreter::CommandChangeCondition(RPG::EventCommand const& com) { // Code 10480
	bool remove = com.parameters[2] != 0;
	int state_id = com.parameters[3];

	for (const auto& actor : GetActors(com.parameters[0], com.parameters[1])) {
		if (remove) {
			actor->RemoveState(state_id);
		} else {
			if(state_id == 1) {
				actor->ChangeHp(-actor->GetHp());
			}
			actor->AddState(state_id);
			CheckGameOver();
		}
	}

	return true;
}

bool Game_Interpreter::CommandFullHeal(RPG::EventCommand const& com) { // Code 10490
	for (const auto& actor : GetActors(com.parameters[0], com.parameters[1])) {
		actor->ChangeHp(actor->GetMaxHp());
		actor->SetSp(actor->GetMaxSp());
		actor->RemoveAllStates();
	}

	return true;
}

bool Game_Interpreter::CommandPlayBGM(RPG::EventCommand const& com) { // code 11510
	RPG::Music music;
	music.name = com.string;
	music.fadein = com.parameters[0];
	music.volume = com.parameters[1];
	music.tempo = com.parameters[2];
	music.balance = com.parameters[3];
	Game_System::BgmPlay(music);
	return true;
}

bool Game_Interpreter::CommandFadeOutBGM(RPG::EventCommand const& com) { // code 11520
	int fadeout = com.parameters[0];
	Audio().BGM_Fade(fadeout);
	return true;
}

bool Game_Interpreter::CommandPlaySound(RPG::EventCommand const& com) { // code 11550
	RPG::Sound sound;
	sound.name = com.string;
	sound.volume = com.parameters[0];
	sound.tempo = com.parameters[1];
	sound.balance = com.parameters[2];
	Game_System::SePlay(sound);
	return true;
}

bool Game_Interpreter::CommandTintScreen(RPG::EventCommand const& com) { // code 11030
	Game_Screen* screen = Main_Data::game_screen.get();
	int r = com.parameters[0];
	int g = com.parameters[1];
	int b = com.parameters[2];
	int s = com.parameters[3];
	int tenths = com.parameters[4];
	bool wait = com.parameters[5] != 0;

	screen->TintScreen(r, g, b, s, tenths);

	if (wait)
		SetupWait(tenths);

	return true;
}

bool Game_Interpreter::CommandFlashScreen(RPG::EventCommand const& com) { // code 11040
	Game_Screen* screen = Main_Data::game_screen.get();
	int r = com.parameters[0];
	int g = com.parameters[1];
	int b = com.parameters[2];
	int s = com.parameters[3];
	int tenths = com.parameters[4];
	bool wait = com.parameters[5] != 0;

	if (com.parameters.size() <= 6) {
		screen->FlashOnce(r, g, b, s, tenths);
		if (wait)
			SetupWait(tenths);
	} else {
		switch (com.parameters[6]) {
		case 0:
			screen->FlashOnce(r, g, b, s, tenths);
			if (wait)
				SetupWait(tenths);
			break;
		case 1:
			screen->FlashBegin(r, g, b, s, tenths);
			break;
		case 2:
			screen->FlashEnd();
			break;
		}
	}

	return true;
}

bool Game_Interpreter::CommandShakeScreen(RPG::EventCommand const& com) { // code 11050
	Game_Screen* screen = Main_Data::game_screen.get();
	int strength = com.parameters[0];
	int speed = com.parameters[1];
	int tenths = com.parameters[2];
	bool wait = com.parameters[3] != 0;

	if (Player::IsRPG2k()) {
		screen->ShakeOnce(strength, speed, tenths);
		if (wait) {
			SetupWait(tenths);
		}
	} else {
		switch (com.parameters[4]) {
			case 0:
				screen->ShakeOnce(strength, speed, tenths);
				if (wait) {
					SetupWait(tenths);
				}
				break;
			case 1:
				screen->ShakeBegin(strength, speed);
				break;
			case 2:
				screen->ShakeEnd();
				break;
		}
	}

	return true;
}

bool Game_Interpreter::CommandEndEventProcessing(RPG::EventCommand const& /* com */) { // code 12310
	index = list.size();
	return true;
}

bool Game_Interpreter::DefaultContinuation(RPG::EventCommand const& /* com */) {
	continuation = NULL;
	index++;
	return true;
}

bool Game_Interpreter::CommandGameOver(RPG::EventCommand const& /* com */) { // code 12420
	Game_Temp::gameover = true;
	SetContinuation(&Game_Interpreter::DefaultContinuation);
	return false;
}

// Dummy Continuations

bool Game_Interpreter::ContinuationOpenShop(RPG::EventCommand const& /* com */) { return true; }
bool Game_Interpreter::ContinuationShowInnStart(RPG::EventCommand const& /* com */) { return true; }
bool Game_Interpreter::ContinuationShowInnFinish(RPG::EventCommand const& /* com */) { return true; }
bool Game_Interpreter::ContinuationEnemyEncounter(RPG::EventCommand const& /* com */) { return true; }