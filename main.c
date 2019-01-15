#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <X11/keysym.h>

#define TABLENGTH(X)		(sizeof(X)/sizeof(*X))
#define XCB_MOVE_RESIZE		XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT
#define XCB_MOVE		XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y
#define XCB_RESIZE		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT

typedef union
{
	const char **com;
	const int i;
} Arg;

typedef struct
{
	unsigned int mod;
	xcb_keysym_t keysym;
	void (*func)(const Arg arg);
	const Arg arg;
} Key;

typedef struct
{
	unsigned int mask, button;
	void (*func)(const Arg arg);
	const Arg arg;
} Button;

int sigcode;

xcb_connection_t *conn;
xcb_ewmh_connection_t *ewmh;
xcb_screen_t *screen;
xcb_generic_event_t *event;
xcb_window_t lastfocus;
xcb_key_symbols_t *keysyms;

static xcb_screen_t * xcb_screen_of_display( xcb_connection_t *, int );
static xcb_keysym_t xcb_get_keysym( xcb_keycode_t );

static void spawn( const Arg );
static void killclient( xcb_window_t * );
static void mapwindow( xcb_map_request_event_t * );
static void keypress( xcb_key_press_event_t * );
static void buttonpress( xcb_button_press_event_t * );
static void focuswin( xcb_window_t );
static void xcb_move_resize( xcb_window_t, int, int, int, int );
static void xcb_move( xcb_window_t, int, int );
static void xcb_resize( xcb_window_t, int, int );
static void grabkeys( void );
static void handle_events( void );
static short setup( int );
static void run( void );
static void closewm( void );

#include "config.h"

xcb_screen_t * xcb_screen_of_display( xcb_connection_t *con, int screen )
{
	xcb_screen_iterator_t iter;
	iter = xcb_setup_roots_iterator( xcb_get_setup( con ) );
	for( ; iter.rem; --screen, xcb_screen_next(&iter) )
		if( screen == 0 )
			return iter.data;

	return NULL;
}

xcb_keysym_t xcb_get_keysym( xcb_keycode_t keycode )
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
	if (fork())
        	return;
                                                      
        if (conn)
        	close(screen->root);
                                                      
        setsid();
        execvp((char*)arg.com[0], (char**)arg.com);
}

void killclient ( xcb_window_t *window )
{
	xcb_kill_client( conn, *window );
}

void mapwindow( xcb_map_request_event_t *e )
{
	xcb_window_t win = e -> window;

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
	xcb_change_property( conn, XCB_PROP_MODE_REPLACE, e -> window, ewmh -> _NET_WM_STATE, ewmh -> _NET_WM_STATE, 32, 2, data);
}

void focuswin( xcb_window_t win )
{
	xcb_set_input_focus( conn, XCB_INPUT_FOCUS_NONE, win, XCB_CURRENT_TIME );
}

static void xcb_move_resize( xcb_window_t win, int x, int y, int w, int h )
{
	unsigned int pos[4] = { x, y, w, h };
	xcb_configure_window( conn, win, XCB_MOVE_RESIZE, pos );
}

static void xcb_move( xcb_window_t win, int x, int y )
{
	unsigned int pos[2] = { x, y };
	xcb_configure_window( conn, win, XCB_MOVE, pos );
}

static void xcb_resize( xcb_window_t win, int w, int h )
{
	unsigned int pos[2] = { w, h };
	xcb_configure_window( conn, win, XCB_RESIZE, pos );
}

void grabkeys( void )
{
	xcb_keycode_t *code = NULL;
	xcb_ungrab_key( conn, XCB_GRAB_ANY, screen -> root, XCB_MOD_MASK_ANY );

	for(int i = 0; i < TABLENGTH(keys); ++i) {
		if((code = xcb_key_symbols_get_keycode(keysyms, keys[i].keysym))) {
			xcb_grab_key( conn, 1, screen -> root, keys[i].mod, *code, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC );
		}
	}
}

void keypress( xcb_key_press_event_t *e )
{
       	xcb_keysym_t keysym = xcb_key_symbols_get_keysym( keysyms, e -> detail, 0 );
       	for(int i = 0; i < TABLENGTH(keys); ++i)
       		if(keys[i].keysym == keysym && keys[i].mod == e -> state)
       			keys[i].func(keys[i].arg);
}

void buttonpress( xcb_button_press_event_t *e )
{
       	for(int i = 0; i < TABLENGTH(buttons); ++i)
       		if(buttons[i].button == e -> detail && buttons[i].mask == e -> state)
       			buttons[i].func(buttons[i].arg);

	if( CLICK_TO_FOCUS && e->detail == XCB_BUTTON_INDEX_1 ) {
		focuswin( e->event );
	}
}

void handle_events( void )
{
	xcb_flush( conn );
	event = xcb_wait_for_event( conn );

	switch( event -> response_type )
	{
		case XCB_MAP_REQUEST:
			mapwindow( (xcb_map_request_event_t *) event );
			break;
		case XCB_UNMAP_NOTIFY:
			break;
		case XCB_KEY_PRESS:
			keypress( (xcb_key_press_event_t *) event );
			break;
		case XCB_BUTTON_PRESS:
			buttonpress( (xcb_button_press_event_t *) event );
			break;
		default:
			break;
	}
}

short setup( int scrno )
{
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

	if ( !(keysyms = xcb_key_symbols_alloc( conn )) ) {
		fprintf( stderr, "Keysyms could not be allocated.\n" );
		return 1;
	}

	free( error );
	grabkeys();

	return 0;
}

void run( void )
{
	while( sigcode == 0 ) { handle_events( ); }
}

void closewm( void )
{
	fprintf( stderr, "Closing bluewm...\n" );
	xcb_disconnect( conn );
}

int main( int argc, char **argv )
{
	int scrno;
	if( !xcb_connection_has_error( conn = xcb_connect( NULL, &scrno ) ) ) {
		if( setup( scrno ) == 0 ) {
			run();
		} else {
			fprintf( stderr, "Failed to setup bluewm.\n" );
		}
	}

	closewm();
	return sigcode;
}
