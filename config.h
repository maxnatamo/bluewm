#pragma once

#define MOD		XCB_MOD_MASK_4

const char *termcmd[] = { "urxvt", NULL };
const char *menucmd[] = { "wal", "-i", "~/Wallpapers", NULL };
const char *browser[] = { "qutebrowser", NULL };

static struct key keys[] = {
	{ MOD,		XK_Return,		spawn,		{.com = termcmd} },
	{ MOD,		XK_space,		spawn,		{.com = menucmd} }
};
