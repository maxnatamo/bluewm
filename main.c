#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <X11/keysym.h>

#include "config.h"

// Variable declarations
int sigcode;

xcb_connection_t *conn;
xcb_ewmh_connection_t *ewmh;
xcb_screen_t *screen;
xcb_generic_event_t *event;
xcb_window_t lastfocus;

// Function declarations.
static xcb_screen_t * xcb_screen_of_display( xcb_connection_t *, int );
static void spawn( const char *cmd[] );
static void killclient( xcb_window_t * );
static void mapwindow( xcb_generic_event_t * );
static void handle_events( void );
static short setup( int );
static void run( void );
static void closewm( void );

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

//void
//spawn( const char *cmd[] )
//{	
//	if(fork() == 0)
//	{
//		setsid();
//		execvp( ( (char **) cmd )[0], ( char ** ) cmd );
//		printf( "%s has been spawned.\n", cmd[0] );
//	}
//}

void
spawn(const char *cmd[])
{
	if (fork())
		return;

	if (conn)
		close(screen->root);

	setsid();
	execvp((char*)cmd[0], (char**)cmd);
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

void
setfocus(xcb_window_t *window)
{
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_NONE, window, XCB_CURRENT_TIME);
}

void
keypress( xcb_generic_event_t *e )
{
	xcb_key_press_event_t *ev 	= (xcb_key_press_event_t *) e;
	xcb_keysym_t keysym 		= xcb_get_keysym( ev -> detail );
	xcb_window_t root		= ev -> root;

	switch( keysym )
	{
		case XK_Return:		spawn( termcmd );			break;
		case XK_space:		spawn( menucmd );			break;
		case XK_b:		spawn( browser );			break;
		case XK_q:		killclient( &root );			break;
		default:							break;
	}
}

void
buttonpress( xcb_generic_event_t *e )
{
	xcb_button_press_event_t *ev	= (xcb_button_press_event_t *) e;

	switch( ev -> detail )
	{
		case 1:
			// Left mouse button.
			setfocus( ev -> child );
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
	// Thanks 2bwm.
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
		return 0;
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
		return 0;
	}

	return 1;
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
		if( setup( scrno ) == 1 ) {
			run( );
		} else {
			fprintf( stderr, "Failed to setup bluewm. Exiting..." );
		}
	}

	closewm();
	return sigcode;
}
