#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <X11/keysym.h>

#define TABLENGTH(X)	(sizeof(X)/sizeof(*X))

typedef union
{
	const char **com;
	const int i;
} Arg;

struct key
{
	unsigned int mod;
	xcb_keysym_t keysym;
	void (*function)(const Arg arg);
	const Arg arg;
};

int sigcode;

xcb_connection_t *conn;
xcb_ewmh_connection_t *ewmh;
xcb_screen_t *screen;
xcb_generic_event_t *event;
xcb_window_t lastfocus;
xcb_key_symbols_t *keysyms;

static xcb_screen_t * xcb_screen_of_display( xcb_connection_t *, int );
static void spawn( const Arg );
static void killclient( xcb_window_t * );
static void mapwindow( xcb_generic_event_t * );
static void grabkeys( void );
static void handle_events( void );
static short setup( int );
static void run( void );
static void closewm( void );

#include "config.h"

xcb_screen_t *
xcb_screen_of_display( xcb_connection_t *con, int screen )
{
	xcb_screen_iterator_t iter;
	iter = xcb_setup_roots_iterator( xcb_get_setup( con ) );
	for( ; iter.rem; --screen, xcb_screen_next(&iter) )
		if( screen == 0 )
			return iter.data;

	return NULL;
}

xcb_keysym_t
xcb_get_keysym( xcb_keycode_t keycode )
{
	xcb_key_symbols_t *keysyms;

	if(!(keysyms = xcb_key_symbols_alloc(conn)))
		return 0;

	xcb_keysym_t keysym = xcb_key_symbols_get_keysym( keysyms, keycode, 0 );
	xcb_key_symbols_free( keysyms );

	return keysym;
}

void spawn(const Arg arg)
{
	int pid;
	if((pid = fork()) == 0)
	{
		if(fork() == 0)
		{
			if(conn)
				close(  xcb_get_file_descriptor( conn ) );

			setsid();
			execvp( (char *) arg.com[0], (char **) arg.com );
		}
		exit( 0 );
	} else {
		printf("Error on opening program with return code %s\n", (char)pid);
	}
}

void
killclient ( xcb_window_t *window )
{
	xcb_kill_client( conn, *window );
}

void
mapwindow( xcb_generic_event_t *e )
{

	xcb_map_request_event_t *ev = (xcb_map_request_event_t *) e;
	xcb_window_t win = ev -> window;

	// Thanks 2bwm.
	uint32_t values[2];
	xcb_atom_t a;
        xcb_size_hints_t hints;
        xcb_ewmh_get_atoms_reply_t win_type;

	long data[] = {
        	XCB_ICCCM_WM_STATE_NORMAL,
        	XCB_NONE
        };

	xcb_map_window( conn, win );

        if (xcb_ewmh_get_wm_window_type_reply(ewmh, xcb_ewmh_get_wm_window_type(ewmh, win), &win_type, NULL) == 1) {
        	for (unsigned int i = 0; i < win_type.atoms_len; i++) {
        		a = win_type.atoms[i];
        		if (a == ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR || a
        				== ewmh->_NET_WM_WINDOW_TYPE_DOCK || a
        				== ewmh->_NET_WM_WINDOW_TYPE_DESKTOP ) {
        			xcb_ewmh_get_atoms_reply_wipe(&win_type);
        			xcb_map_window(conn,win);
        		}
        	}
	}
                                                                                 
        values[0] = XCB_EVENT_MASK_ENTER_WINDOW;
        xcb_change_window_attributes_checked(conn, win, XCB_CW_EVENT_MASK, values);
	xcb_change_property( conn, XCB_PROP_MODE_REPLACE, ev -> window, ewmh -> _NET_WM_STATE, ewmh -> _NET_WM_STATE, 32, 2, data);
}

void grabkeys()
{
	xcb_keycode_t *code = NULL;
	xcb_ungrab_key( conn, XCB_GRAB_ANY, screen -> root, XCB_MOD_MASK_ANY );

	for(int i = 0; i < TABLENGTH(keys); ++i) {
		if((code = xcb_key_symbols_get_keycode(keysyms, keys[i].keysym))) {
			xcb_grab_key( conn, 1, screen -> root, keys[i].mod, *code, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC );
		}
	}
}

void
keypress( xcb_generic_event_t *e )
{
	xcb_key_press_event_t *ev 	= (xcb_key_press_event_t *) e;
       	xcb_keysym_t keysym = xcb_key_symbols_get_keysym( keysyms, ev -> detail, 0 );
        
       	for(int i = 0; i < TABLENGTH(keys); ++i)
       		if(keys[i].keysym == keysym && keys[i].mod == ev -> state)
       			keys[i].function(keys[i].arg);
}

void
buttonpress( xcb_generic_event_t *e )
{
	xcb_button_press_event_t *ev	= (xcb_button_press_event_t *) e;

	switch( ev -> detail )
	{
		case 1:
			// Left mouse button.
			break;
		case 3:
			// Right mouse button.
			break;
		default:
			break;
	}
}

void
handle_events( void )
{
	event = xcb_wait_for_event( conn );

	switch( event -> response_type )
	{
		case XCB_MAP_REQUEST:
			mapwindow(event);
			break;
		case XCB_KEY_PRESS:
			keypress(event);
			break;
		case XCB_BUTTON_PRESS:
			buttonpress(event);
			break;
		default:
			break;
	}
}

// setup: returns 0 on error, 1 otherwise.
short
setup( int scrno )
{
	uint32_t event_mask_pointer[] = { XCB_EVENT_MASK_POINTER_MOTION };

	unsigned int values[1] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
		| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
		| XCB_EVENT_MASK_PROPERTY_CHANGE
		| XCB_EVENT_MASK_BUTTON_PRESS
		| XCB_EVENT_MASK_KEY_PRESS
	};

	screen = xcb_screen_of_display( conn, scrno );

	if ( !screen ) {
		fprintf( stderr, "Failed to retrieve screen of main display.\n" );
		return 1;
	}

	if( !(ewmh = calloc( 1, sizeof( xcb_ewmh_connection_t ) ) ) ) {
		fprintf( stderr, "Failed to allocate EWMH connection.\n" );
	}

	xcb_intern_atom_cookie_t *cookie = xcb_ewmh_init_atoms( conn, ewmh );
	xcb_ewmh_init_atoms_replies( ewmh, cookie, (void *)0 );

	xcb_ewmh_set_wm_pid( ewmh, screen -> root, getpid() );
	xcb_ewmh_set_wm_name( ewmh, screen -> root, 4, "bluewm" );

	xcb_generic_error_t *error = xcb_request_check( conn, xcb_change_window_attributes_checked( conn, screen -> root, XCB_CW_EVENT_MASK, values ) );
	xcb_flush( conn );

	if(error) {
		fprintf( stderr, "Failed to change window attributes.\n" );
		return 1;
	}
	free( error );

	if ( !(keysyms = xcb_key_symbols_alloc( conn )) ) {
		fprintf( stderr, "Keysyms could not be allocated.\n" );
		return 1;
	}

	grabkeys();

	return 0;
}

void
run( void )
{
	while( sigcode == 0 ) { handle_events( ); }
}

void
closewm( void )
{
	fprintf( stderr, "Closing bluewm.\n" );
	xcb_disconnect( conn );
}

int
main( int argc, char **argv )
{
	int scrno;
	if( !xcb_connection_has_error( conn = xcb_connect( NULL, &scrno ) ) ) {
		if( setup( scrno ) == 0 ) {
			run( );
		} else {
			fprintf( stderr, "Failed to setup bluewm. Exiting..." );
		}
	}

	closewm();
	return sigcode;
}
