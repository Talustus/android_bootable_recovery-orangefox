/*
	Copyright 2013 to 2017 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <fcntl.h>
#include <string>
#include <unistd.h>
#include "data.hpp"
#include "partitions.hpp"
#include "set_metadata.h"
#include "twrpDigestDriver.hpp"
#include "twrp-functions.hpp"
#include "twcommon.h"
#include "variables.h"
#include "gui/gui.hpp"
#include "twrpDigest/twrpDigest.hpp"
#include "twrpDigest/twrpMD5.hpp"
#include "twrpDigest/twrpSHA.hpp"

std::vector<string> PartFilenames; 

bool twrpDigestDriver::Check_Restore_File_Digest(const string& Filename) {
	twrpDigest *digest;
	string digestfile = Filename, file_name = Filename;
	string digest_str;
	bool use_sha2 = false;

#ifndef TW_NO_SHA2_LIBRARY

	digestfile += ".sha2";
	if (TWFunc::Path_Exists(digestfile)) {
		digest = new twrpSHA256();
		use_sha2 = true;
	}
	else {
		digest = new twrpMD5();
		digestfile = Filename + ".md5";

	}
#else
	digest = new twrpMD5();
	digestfile = Filename + ".md5";

#endif

	if (!TWFunc::Path_Exists(digestfile)) {
		delete digest;
		if (Filename.find(".zip") == std::string::npos && Filename.find(".map") == std::string::npos) {
			gui_msg(Msg(msg::kError, "no_digest_found=No digest file found for '{1}'. Please unselect Enable Digest verification to restore.")(Filename));
		} else {
			return true;
		}
		return false;
	}


	if (TWFunc::read_file(digestfile, digest_str) != 0) {
		gui_msg("digest_error=Digest Error!");
		delete digest;
		return false;
	}

	if (!stream_file_to_digest(file_name, digest)) {
		delete digest;
		return false;
	}
	string digest_check = digest->return_digest_string();
	if (digest_check == digest_str) {
		if (use_sha2)
			LOGINFO("SHA2 Digest: %s  %s\n", digest_str.c_str(), TWFunc::Get_Filename(Filename).c_str());
		else
			LOGINFO("MD5 Digest: %s  %s\n", digest_str.c_str(), TWFunc::Get_Filename(Filename).c_str());
		delete digest;
		return true;
	}

	gui_msg(Msg(msg::kError, "digest_fail_match=Digest failed to match on '{1}'.")(Filename));
	delete digest;
	return false;

}

bool twrpDigestDriver::Check_Digest(string Full_Filename) {
	char split_filename[512];
	int index = 0;

	sync();
	if (!TWFunc::Path_Exists(Full_Filename)) {
		// This is a split archive, we presume
		memset(split_filename, 0, sizeof(split_filename));
		while (index < 1000) {
			sprintf(split_filename, "%s%03i", Full_Filename.c_str(), index);
			if (!TWFunc::Path_Exists(split_filename))
				break;
				LOGINFO("split_filename: %s\n", split_filename);
				if (!Check_Restore_File_Digest(split_filename))
					return false;
				index++;
		}
		return true;
	}
	return Check_Restore_File_Digest(Full_Filename); // Single file archive
}

bool twrpDigestDriver::Write_Digest(string Full_Filename) {
	twrpDigest *digest;
	string digest_filename, digest_str;
	int use_sha2;

#ifdef TW_NO_SHA2_LIBRARY
	use_sha2 = 0;
#else
	DataManager::GetValue(TW_USE_SHA2, use_sha2);
#endif

	if (use_sha2) {
#ifndef TW_NO_SHA2_LIBRARY
		digest = new twrpSHA256();
		digest_filename = Full_Filename + ".sha2";
		if (!stream_file_to_digest(Full_Filename, digest)) {
			delete digest;
			return false;
		}
		digest_str = digest->return_digest_string();
		if (digest_str.empty()) {
			delete digest;
			return false;
		}
		LOGINFO("SHA2 Digest: %s  %s\n", digest_str.c_str(), TWFunc::Get_Filename(Full_Filename).c_str());
#endif
	}
	else  {
		digest = new twrpMD5();
		digest_filename = Full_Filename + ".md5";
		if (!stream_file_to_digest(Full_Filename, digest)) {
			delete digest;
			return false;
		}
		digest_str = digest->return_digest_string();
		if (digest_str.empty()) {
			delete digest;
			return false;
		}
		LOGINFO("MD5 Digest: %s  %s\n", digest_str.c_str(), TWFunc::Get_Filename(Full_Filename).c_str());
	}

	digest_str = digest_str + "  " + TWFunc::Get_Filename(Full_Filename) + "\n";
	LOGINFO("digest_filename: %s\n", digest_filename.c_str());

	if (TWFunc::write_to_file(digest_filename, digest_str) == 0) {
		tw_set_default_metadata(digest_filename.c_str());
		gui_msg("digest_created= * Digest Created.");
	}
	else {
		gui_err("digest_error= * Digest Error!");
		delete digest;
		return false;
	}
	delete digest;
	return true;
}

void twrpDigestDriver::Add_Digest(string Full_Filename) {
	LOGINFO("Pushing to list: %s\n", Full_Filename.c_str());
	PartFilenames.push_back(Full_Filename);
}

void twrpDigestDriver::CleanList() {
	PartFilenames.clear();
}

int twrpDigestDriver::Run_Digest() { //translate
	gui_msg("con_digest_start=[DIGEST CREATION STARTED]");

	if (PartFilenames.empty()) {
		gui_msg(Msg(msg::kError, "con_digest_error=Partition list is empty!"));
		return 1;	
	}

	DataManager::SetValue(TW_ACTION_BUSY, "1");
	bool ret_val = 0;
	int vector_size = PartFilenames.size();
  	time_t seconds, total_start, total_stop;

    DataManager::SetValue("ui_progress", 0);

  	time(&total_start);

	for (int i = 0; i < vector_size; i++) {
		gui_print("%s\n", basename(PartFilenames[i].c_str()));
        if (!twrpDigestDriver::Make_Digest(PartFilenames[i])) {
			ret_val = 1;
			break;
		}

		int progress = (int) (((float) (vector_size - i) / (float) vector_size) * 100.0);
    	DataManager::SetValue("ui_progress", progress);
    }
	PartFilenames.clear();
	DataManager::SetValue("fox_show_digest_btn", "0");
	DataManager::SetValue(TW_ACTION_BUSY, "0");

  	time(&total_stop);
  	int total_time = (int) difftime(total_stop, total_start);
	gui_msg(Msg(msg::kHighlight, "con_digest_complete=[DIGEST CREATION COMPLETED IN {1} SECONDS]") (total_time));

	return ret_val;
}

bool twrpDigestDriver::Make_Digest(string Full_Filename) {
	string command, result;

	TWFunc::GUI_Operation_Text(TW_GENERATE_DIGEST_TEXT, gui_parse_text("{@generating_digest1}"));
	gui_msg("generating_digest2= * Generating digest...");
	if (TWFunc::Path_Exists(Full_Filename)) {
		if (!Write_Digest(Full_Filename))
			return false;
	} else {
		char filename[512];
		int index = 0;
		sprintf(filename, "%s%03i", Full_Filename.c_str(), index);
		while (index < 1000) {
			string digest_src(filename);
			if (TWFunc::Path_Exists(filename)) {
				if (!Write_Digest(filename))
					return false;
				}
				else
					break;
				index++;
				sprintf(filename, "%s%03i", Full_Filename.c_str(), index);
			}
			if (index == 0) {
				LOGERR("Backup file: '%s' not found!\n", filename);
					return false;
			}
			gui_msg("digest_created= * Digest Created.");
	}
	return true;
}

bool twrpDigestDriver::stream_file_to_digest(string filename, twrpDigest* digest) {
	char buf[4096];
	int bytes;

	int fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0) {
		return false;
	}
	while ((bytes = read(fd, &buf, sizeof(buf))) != 0) {
		digest->update((unsigned char*)buf, bytes);
	}
	close(fd);
	return true;
}
