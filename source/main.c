/**
 * ctrmus - 3DS Music Player
 * Copyright (C) 2016 Mahyar Koshkouei
 *
 * This program comes with ABSOLUTELY NO WARRANTY and is free software. You are
 * welcome to redistribute it under certain conditions; for details see the
 * LICENSE file.
 */

#include <3ds.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sf2d.h>
#include <sftd.h>
#include <time.h>

#include "all.h"
#include "main.h"
#include "sync.h"
#include "playback.h"

#define STACKSIZE (16 * 1024)

bool debug = false;

sftd_font* fontR;
sftd_font* fontB;
int fontSize = 15;

// UI to PLAYER
bool run = true;
int nowPlaying = -1;

// PLAYER to UI
volatile float progress = 0;

int nbDirs;
int nbFiles;
int nbFolderNames;
int nbListNames = 0;
char** foldernames = NULL;
char** listnames = NULL;

int debugInt = 0;
int heldListIndex = -1;
int emptyListItemIndex = -1;
int emptyListItemSize = -1;

void addToPlaylist(char* filepath) {
	char** newListNames = (char**)malloc((nbListNames+1)*sizeof(char*));
	memcpy(newListNames, listnames, nbListNames*sizeof(char*));
	newListNames[nbListNames] = filepath;
	if (nbListNames != 0) free(listnames);
	nbListNames++;
	listnames = newListNames;
}
void insertInList(int j, char* s) {
	addToPlaylist(s); // insert an emtpy line at the end
	char* temp = listnames[nbListNames-1]; // move it from the end to here
	for (int i=nbListNames-1; i>j; i--) listnames[i] = listnames[i-1];
	listnames[j] = temp;
}

void listLongClicked(int hilit, bool released, int deltaX) {
	debugInt = deltaX;
	if (hilit < nbListNames) {
		if (released) {
			if (deltaX > 100) {
				if (heldListIndex == hilit) heldListIndex = -1;
				if (nowPlaying == hilit) nowPlaying = -1;
				if (nowPlaying > hilit) nowPlaying--;
				emptyListItemIndex = hilit;
				emptyListItemSize = 240; // will be lowered
				free(listnames[hilit]);
				nbListNames--;
				char** newlistnames = malloc(nbListNames*sizeof(char*));
				memcpy(newlistnames, listnames, hilit*sizeof(char*));
				memcpy(newlistnames+hilit, listnames+hilit+1, (nbListNames-hilit)*sizeof(char*));
				free(listnames);
				listnames = newlistnames;
			}
		} else {
			heldListIndex = heldListIndex == hilit ? -1 : hilit;
		}
	}
}

void listClicked(int hilit) {
	if (hilit < nbListNames) {
		if (heldListIndex == -1) {
			nowPlaying = hilit;
			startPlayingFile(listnames[hilit]);
		} else {
			if (hilit == heldListIndex) {
				insertInList(hilit, strdup(""));
			} else {
				char* temp = listnames[hilit];
				listnames[hilit] = listnames[heldListIndex];
				listnames[heldListIndex] = temp;
				if (nowPlaying == heldListIndex) {
					nowPlaying = hilit;
				} else if (nowPlaying == hilit) {
					nowPlaying = heldListIndex;
				}
			}
			heldListIndex = -1;
		}
	}
}

void folderLongClicked(int hilit, bool released, int deltaX) {
}

void folderClicked(int hilit) {
	if (hilit < nbDirs) {
		chdir(foldernames[hilit]);
		updateFolderContents();
	} else if (hilit < nbFolderNames) { // don't detect' clicks below the list
		char* wd = getcwd(NULL, 0);
		int lw = strlen(wd);
		int lb = strlen(foldernames[hilit]);
		int l = lw + lb;
		char* filepath = (char*)malloc((l+1)*sizeof(char));
		memcpy(filepath, wd, lw);
		memcpy(filepath+lw, foldernames[hilit], lb);
		filepath[l] = 0;
		free(wd);
		addToPlaylist(filepath);
	}
}

#define SCROLL_THRESHOLD 15.0f

void updateList(
	touchPosition *touchPad, touchPosition *oldTouchPad, touchPosition *orgTouchPad, bool touchPressed, bool touchWasPressed, u64 orgTimeTouched,
	int *hilit, float y, float *vy, void (*clicked)(int hilit), void (*longclicked)(int hilit, bool released, int deltaX),
	int cellSize, float inertia,
	float paneBorderGoal, float orgPaneBorderGoal
) {
	static bool ignoreTouch = false;
	static int deltaX = 0;

	if (touchPressed) {
		if (!touchWasPressed) {
			*hilit = (y+touchPad->py)/cellSize;
		} else {
			if (fabs((float)(touchPad->px-orgTouchPad->px))>SCROLL_THRESHOLD) {
				deltaX = touchPad->px-orgTouchPad->px;
				ignoreTouch = true;
			} else if (fabs((float)(touchPad->py-orgTouchPad->py))>SCROLL_THRESHOLD) {
				orgTouchPad->py = 256; // keep scrolling even if we come back near the "real" original pos
				*vy = oldTouchPad->py - touchPad->py;
			} else {
				if (!ignoreTouch) {
					if (osGetTime()-orgTimeTouched > 700) {
						(*longclicked)(*hilit, false, 0);
						ignoreTouch = true;
					}
				}
			}
		}
	} else {
		if (touchWasPressed) { // keys were just released
			if (orgTouchPad->py != 256) { // we didn't scroll
				if (paneBorderGoal == orgPaneBorderGoal) { // not the first time we click on that panel to bring it to focus
					if (ignoreTouch) { // long click or swipe
						(*longclicked)(*hilit, true, fabs((float)deltaX));
						deltaX = 0;
					} else {
						(*clicked)(*hilit);
					}
				}
			}
		}
		ignoreTouch = false;
	}
}

char* basename(char* s) {
	int i = strlen(s);
	while (i!=-1 && s[i] != '/') i--;
	return s+i+1;
}

void loadPlaylist(char* filename) {
	FILE* f = fopen(filename, "r");
	if (f == NULL) return;

	char* line = malloc(2000*sizeof(char));
	while (fgets(line, 2000, f) != NULL) {
		line[strlen(line)-1] = 0; // remove '\n'
		addToPlaylist(strdup(line));
	}
	free(line);
}

void savePlaylist(char* filename) {
	FILE* f = fopen(filename,"wb+");
	if (f == NULL) return;
	for (int i=0; i<nbListNames; i++) {
		fputs(listnames[i], f);
		fputc('\n', f);
	}
	fclose(f);
}

int countCharStars(char** t) {
	int n = 0;
	while (t[n]!=NULL) n++;
	return n;
}

int main(int argc, char** argv)
{
	sftd_init();
	sf2d_init();
	romfsInit();
	sdmcInit();

	FILE* file = fopen("ctrmus_debug","rb");
	if (file != NULL) {
		debug = true;
		fclose(file);
	};

	loadPlaylist("sdmc:/playlist.mdcq");

	chdir(DEFAULT_DIR);
	chdir("Music");

	sf2d_set_clear_color(RGBA8(255,106,0, 255));
	sf2d_set_vblank_wait(0);
	fontR = sftd_load_font_file("romfs:/Ubuntu-R.ttf");
	fontB = sftd_load_font_file("romfs:/Ubuntu-B.ttf");

	aptSetSleepAllowed(false);

	//startPlayingFile("sdmc:/Music/03 - Rosalina.mp3");
	startPlayingFile("romfs:/noise.wav");

	updateFolderContents();

	touchPosition oldTouchPad;
	touchPosition orgTouchPad;
	oldTouchPad.px = 0;
	oldTouchPad.py = 0;
	orgTouchPad = oldTouchPad;
	u64 orgTimeTouched = 0;

	float yFolder = -100; // will go through a minmax, don't worry
	float vyFolder = 0;

	float yList = -100; // will go through a minmax, don't worry
	float vyList = 0;

	float inertia = 10;

	int cellSize = fontSize*2;

	int hilitFolder = -1;
	int hilitList = -1;

	float paneBorder = 160.0f;
	float paneBorderGoal = 160.0f;
	float orgPaneBorderGoal = 160.0f;

	// may be loaded from a config file or something
	u32 bgColor = RGBA8(0,0,0,255);
	u32 lineColor = RGBA8(255,106,0,255);
	u32 textColor = RGBA8(255,255,255,255);
	u32 hlTextColor = RGBA8(255,0,0,255);
	u32 slTextColor = RGBA8(255,128,0,255);

	sf2d_set_vblank_wait(1);

	int scheduleCount = 0;
	while (aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;

		if ((hidKeysDown() & (KEY_L | KEY_ZR))) {
			nowPlaying = fmax(0, nowPlaying-1);
			if (nowPlaying < nbListNames) {
				startPlayingFile(listnames[nowPlaying]);
			} else {
				nowPlaying = -1;
			}
		}

		if (keepPlayingFile() == 1 || (hidKeysDown() & (KEY_ZL | KEY_R))) {
			nowPlaying++;
			if (nowPlaying < nbListNames) {
				startPlayingFile(listnames[nowPlaying]);
			} else {
				nowPlaying = -1;
			}
		}

		// scroll using touchpad
		touchPosition touchPad;
		hidTouchRead(&touchPad);
		bool touchPressed = touchPad.px+touchPad.py != 0;
		bool touchWasPressed = oldTouchPad.px+oldTouchPad.py != 0;
		if (!touchWasPressed) {
			orgTouchPad = touchPad;
			orgPaneBorderGoal = paneBorderGoal;
			orgTimeTouched = osGetTime();
		}
		if (orgTouchPad.px < paneBorder) {
			if (touchPressed) paneBorderGoal = 320-60;
			updateList(
				&touchPad, &oldTouchPad, &orgTouchPad, touchPressed, touchWasPressed, orgTimeTouched,
				&hilitList, yList, &vyList, listClicked, listLongClicked,
				cellSize, inertia,
				paneBorderGoal, orgPaneBorderGoal
			);
		} else {
			if (touchPressed) paneBorderGoal = 60;
			updateList(
				&touchPad, &oldTouchPad, &orgTouchPad, touchPressed, touchWasPressed, orgTimeTouched,
				&hilitFolder, yFolder, &vyFolder, folderClicked, folderLongClicked,
				cellSize, inertia,
				paneBorderGoal, orgPaneBorderGoal
			);
		}

		yList += vyList;
		vyList = vyList*inertia/(inertia+1);
		yFolder += vyFolder;
		vyFolder = vyFolder*inertia/(inertia+1);

		emptyListItemSize = emptyListItemSize*inertia/(inertia+1);
		emptyListItemSize = fmin(cellSize, emptyListItemSize);

		paneBorder = (paneBorder*inertia+paneBorderGoal*1)/(inertia+1);

		oldTouchPad = touchPad;

		// prevent y from going below 0 and above a certain value here
		yFolder = fmax(-10,fmin(cellSize*nbFolderNames-240,yFolder));
		yList = fmax(-10,fmin(cellSize*nbListNames-240,yList));

		//Print current time
		time_t unixTime = time(NULL);
		struct tm* timeStruct = gmtime((const time_t *)&unixTime);
		int hours = timeStruct->tm_hour;
		int minutes = timeStruct->tm_min;
		int seconds = timeStruct->tm_sec;

		sf2d_start_frame(GFX_TOP, GFX_LEFT);
		{
			/*
			sftd_draw_textf(fontR, 0, fontSize*0, RGBA8(0,0,0,255), fontSize, "hilit Folder: %i", hilitFolder);
			sftd_draw_textf(fontR, 0, fontSize*1, RGBA8(0,0,0,255), fontSize, "hilit List: %i", hilitList);
			sftd_draw_textf(fontR, 0, fontSize*0, RGBA8(0,0,0,255), fontSize, "debugInt: %i", debugInt);
			sftd_draw_textf(fontR, 0, fontSize*1, RGBA8(0,0,0,255), fontSize, "heldListIndex: %i", heldListIndex);
			sftd_draw_textf(fontR, 0, fontSize*2, RGBA8(0,0,0,255), fontSize, "folder number: %i", nbDirs);
			sftd_draw_textf(fontR, 0, fontSize*3, RGBA8(0,0,0,255), fontSize, "file number: %i", nbFiles);
			sftd_draw_textf(fontR, 0, fontSize*4, RGBA8(0,0,0,255), fontSize, "emptyListItemIndex: %i", emptyListItemIndex);
			sftd_draw_textf(fontR, 0, fontSize*5, RGBA8(0,0,0,255), fontSize, "emptyListItemSize: %i", emptyListItemSize);
			*/
			sftd_draw_textf(fontB, 0, fontSize*0, RGBA8(0,0,0,255), fontSize, "%02i:%02i:%02i", hours, minutes, seconds);
		}
		sf2d_end_frame();

		sf2d_start_frame(GFX_BOTTOM, GFX_LEFT);
		{
			// list entries
			sf2d_draw_rectangle(1, 0, paneBorder, 240, bgColor);
			for (int i = (yList+10)/cellSize; i < (240+yList)/cellSize && i<=nbListNames; i++) {
				u32 color = textColor;
				int x = 0;
				int y = i >= emptyListItemIndex ? emptyListItemSize : 0;
				if (paneBorderGoal > 160 && i == hilitList) x += touchPad.px - orgTouchPad.px;
				if (i == hilitList) color = hlTextColor;
				if (i == nowPlaying) color = slTextColor;
				if (i == heldListIndex) sf2d_draw_rectangle(1, y+fmax(0,cellSize*i-yList), paneBorder, cellSize, RGBA8(255,255,255,64));
				sf2d_draw_rectangle(1, y+fmax(0,cellSize*i-yList), paneBorder, 1, lineColor);
				sftd_draw_textf(fontR, x+10, y+cellSize*i-yList, color, fontSize, i==nbListNames ? "" : basename(listnames[i]));
			}

			// folder entries
			sf2d_draw_rectangle(paneBorder, 0, 320-1-(int)paneBorder, 240, bgColor);
			for (int i = (yFolder+10)/cellSize; i < (240+yFolder)/cellSize && i<=nbFolderNames; i++) {
				u32 color = textColor;
				int x = paneBorder;
				if (i==hilitFolder) color = hlTextColor;
				sftd_font* font = i<nbDirs ? fontB:fontR;
				sf2d_draw_rectangle(paneBorder, fmax(0,cellSize*i-yFolder), 320-1-(int)paneBorder, 1, lineColor);
				sftd_draw_textf(font, x+10, cellSize*i-yFolder, color, fontSize, i==nbFolderNames ? "" : foldernames[i]);
			}

			// TODO don't try to draw the text at index i if it doesn't exist (if nb is so low that lists don't fill the screen)

			sf2d_draw_rectangle(paneBorder, 0, 1, 240, RGBA8(255,255,255,255));

			// progress bar
			sf2d_draw_rectangle(0, 0, 320, 10, RGBA8(255,106,0,255));
			sf2d_draw_rectangle(20, 0, progress*(320-20*2), 9, RGBA8(0,0,0,255));
		}
		sf2d_end_frame();

		sf2d_swapbuffers();
	}

	stopPlayingFile();

	savePlaylist("sdmc:/playlist.mdcq");

	freeList(foldernames, nbFolderNames);
	freeList(listnames, nbListNames);

	// TODO kill playback
	run = false;

	sftd_free_font(fontR);
	sftd_free_font(fontB);

	sdmcExit();
	romfsExit();
	sftd_fini();
	sf2d_fini();
	return 0;
}

/**
 * Function used for qsort(), to sort string in alphabetical order (A-Z).
 *
 * \param	p1	First string.
 * \param	p2	Second string.
 * \return		strcasecmp return value.
 */
static int sortName(const void *p1, const void *p2)
{
	return strcasecmp(*(char* const*)p1, *(char* const*)p2);
}

void freeList(char** l, int n) {
	for (int i=0; i<n; i++) free(l[i]);
	free(l);
}

static int updateFolderContents() {
	if (foldernames != NULL) freeList(foldernames, nbFolderNames);

	obtainFoldersSizes(&nbDirs, &nbFiles);
	char** dirs = (char**)malloc(nbDirs*sizeof(char*));
	char** files = (char**)malloc(nbFiles*sizeof(char*));
	obtainFolders(dirs, files, SORT_NAME_AZ);

	// add ".." on top of list and inc nbDirs
	nbDirs++;
	nbFolderNames = nbDirs+nbFiles;
	foldernames = malloc((nbFolderNames)*sizeof(char*));
	foldernames[0] = strdup("..");
	memcpy(foldernames+1, dirs, nbDirs*sizeof(char*));
	memcpy(foldernames+nbDirs, files, nbFiles*sizeof(char*));
	free(dirs);
	free(files);

	return 0;
}

static int obtainFoldersSizes(int *nbDirs, int *nbFiles) {
	DIR*			dp;
	struct dirent*	ep;
	char*			wd = getcwd(NULL, 0);
	int				ret = -1;
	int				num_dirs = 0;
	int				num_files = 0;

	if(wd == NULL) goto err;
	if((dp = opendir(wd)) == NULL) goto err;

	while((ep = readdir(dp)) != NULL) {
		if(ep->d_type == DT_DIR) {
			num_dirs++;
		} else {
			num_files++;
		}
	}
	if(closedir(dp) != 0)
	goto err;

	ret = 0;
	*nbDirs = num_dirs;
	*nbFiles = num_files;

err:
	free(wd);
	return ret;
}
static int obtainFolders(char** dirs, char** files, enum sorting_algorithms sort) {
	DIR*			dp;
	struct dirent*	ep;
	char*			wd = getcwd(NULL, 0);
	int				ret = -1;
	int				num_dirs = 0;
	int				num_files = 0;

	if(wd == NULL)
		goto err;
	if((dp = opendir(wd)) == NULL)
		goto err;

	while((ep = readdir(dp)) != NULL)
	{
		char* temp = strdup(ep->d_name);
		if (temp == NULL) goto err;
		if(ep->d_type == DT_DIR)
		{
			dirs[num_dirs] = temp;
			num_dirs++;
		}
		else
		{
			files[num_files] = temp;
			num_files++;
		}
	}

	if(closedir(dp) != 0)
		goto err;

	if(sort == SORT_NAME_AZ)
	{
		qsort(dirs, num_dirs, sizeof(char*), sortName);
		qsort(files, num_files, sizeof(char*), sortName);
	}
	ret = 0;

err:
	free(wd);
	return ret;
}

/**
 * Obtain array of files and directories in current directory.
 *
 * \param	dirs	Unallocated pointer to store allocated directory names.
 *					This must be freed after use.
 * \param	files	Unallocated pointer to store allocated file names.
 *					This must be freed after use.
 * \param	sort	Sorting algorithm to use.
 * \return			Number of entries in total or negative on error.
 */
static int obtainDir(char** *dirs_, char** *files_, int *num_dirs_, int *num_files_, enum sorting_algorithms sort)
{
	DIR*			dp;
	struct dirent*	ep;
	int				ret = -1;
	char*			wd = getcwd(NULL, 0);
	int				num_dirs = 0;
	int				num_files = 0;

	if(wd == NULL)
		goto err;

	if((dp = opendir(wd)) == NULL)
		goto err;

	char** dirs = NULL;
	char** files = NULL;

	while((ep = readdir(dp)) != NULL)
	{
		char* temp = strdup(ep->d_name);
		if (temp == NULL) goto err;
		if(ep->d_type == DT_DIR)
		{
			dirs = realloc(dirs, num_dirs * sizeof(char*));
			*(dirs + num_dirs) = strdup(ep->d_name);
			num_dirs++;
		}
		else
		{
			files = realloc(files, num_files * sizeof(char*));
			*(files + num_files) = strdup(ep->d_name);
			num_files++;
		}
	}

/*
	if(sort == SORT_NAME_AZ)
	{
		qsort(&dirs, num_dirs, sizeof(char*), sortName);
		qsort(&files, num_files, sizeof(char*), sortName);
	}
*/

	// NULL terminate arrays
	dirs = realloc(dirs, (num_dirs * sizeof(char*)) + 1);
	*(dirs + num_dirs + 1) = NULL;

	files = realloc(files, (num_files * sizeof(char*)) + 1);
	*(files + num_files + 1) = NULL;

	if(closedir(dp) != 0)
		goto err;

	ret = 0;
	*num_dirs_ = num_dirs;
	*num_files_ = num_files;
	*dirs_ = dirs;
	*files_ = files;

err:
	free(wd);
	return ret;
}

/**
 * Free memory used by an array of strings.
 * Call this with dirs and files as parameters to free memory allocated by
 * obtainDir().
 */
static void freeDir(char** strs)
{
	while(*strs != NULL)
	{
		free(*strs);
		strs++;
	}

	free(strs);
	strs = NULL;

	return;
}
