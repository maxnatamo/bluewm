#pragma once

#define MOD		XCB_MOD_MASK_4
#define CONTROL		ControlMask
#define SHIFT		ShiftMask

#define CLICK_TO_FOCUS	1

const char *termcmd[] = { "st", NULL };
const char *menucmd[] = { "dmenu_run", NULL };
const char *browser[] = { "qutebrowser", NULL };

static Key keys[] = {
	{ MOD,		XK_Return,		spawn,		{.com = termcmd} },
	{ MOD,		XK_b,			spawn,		{.com = browser} },
	{ MOD,		XK_space,		spawn,		{.com = menucmd} },
	{ MOD,		XK_q,			killclient,	{NULL} },
	{ MOD|SHIFT,	XK_q,			closewm,	{NULL} },
};

static Button buttons[] = {
	//{ MOD,		1,			spawn,		{.com = browser} }
};
