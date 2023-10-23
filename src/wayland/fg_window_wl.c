/*
 * fg_window_wl.c
 *
 * Window management methods for Wayland
 *
 * Copyright (c) 2015 Manuel Bachmann. All Rights Reserved.
 * Written by Manuel Bachmann, <tarnyko@tarnyko.net>
 * Creation date: Tue Mar 17, 2015
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * MANUEL BACHMANN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define FREEGLUT_BUILDING_LIB
#include <GL/freeglut.h>
#include "../fg_internal.h"
#include "egl/fg_window_egl.h"
#define fghCreateNewContext fghCreateNewContextEGL

extern void fghOnReshapeNotify( SFG_Window *window, int width, int height, GLboolean forceNotify );
void fgPlatformReshapeWindow( SFG_Window *window, int width, int height );
void fgPlatformIconifyWindow( SFG_Window *window );
void fgPlatformCloseWindow( SFG_Window* window );


static void fghXdgSurfaceConfigure( void* data,
                                    struct xdg_surface *xdg_surface,
                                    uint32_t serial )
{
    SFG_Window* window = data;

    printf("fghXdgSurfaceConfigure\n");

    xdg_surface_ack_configure(xdg_surface, serial);

    wl_surface_attach(window->Window.pContext.surface, NULL, 0, 0);
    wl_surface_commit(window->Window.pContext.surface);
}
static const struct xdg_surface_listener fghXdgSurfaceListener =
{
    .configure = fghXdgSurfaceConfigure,
};

static void fghXdgToplevelConfigure(void *data,
                                    struct xdg_toplevel *xdg_toplevel,
                                    int32_t width,
                                    int32_t height,
                                    struct wl_array *states)
{
    SFG_Window* window = data;

    printf("fghXdgToplevelConfigure\n");

    fgPlatformReshapeWindow( window, width, height );

    wl_surface_commit(window->Window.pContext.surface);
}

static void fghXdgToplevelClose(void *data,
                                struct xdg_toplevel *xdg_toplevel)
{
    SFG_Window* window = data;
    fgPlatformCloseWindow(window);
}

static const struct xdg_toplevel_listener fghXdgToplevelListener = {
    .configure = fghXdgToplevelConfigure,
    .close = fghXdgToplevelClose,
};


static int fghToggleFullscreen(void)
{
    SFG_Window* win = fgStructure.CurrentWindow;

    if ( ! win->State.IsFullscreen )
    {
      win->State.pWState.OldWidth = win->State.Width;
      win->State.pWState.OldHeight = win->State.Height;
      xdg_toplevel_set_fullscreen( win->Window.pContext.xdg_toplevel, NULL );
    }
    else
    {
      fgPlatformReshapeWindow( win, win->State.pWState.OldWidth,
                                    win->State.pWState.OldHeight );
      xdg_toplevel_unset_fullscreen( win->Window.pContext.xdg_toplevel );
    }

    return 0;
}

void fgPlatformOpenWindow( SFG_Window* window, const char* title,
                           GLboolean positionUse, int x, int y,
                           GLboolean sizeUse, int w, int h,
                           GLboolean gameMode, GLboolean isSubWindow )
{
    /* Save the display mode if we are creating a menu window */
    if( window->IsMenu && ( ! fgStructure.MenuContext ) )
        fgState.DisplayMode = GLUT_DOUBLE | GLUT_RGB ;

    fghChooseConfig( &window->Window.pContext.egl.Config );

    if( ! window->Window.pContext.egl.Config )
    {
        /*
         * The "fghChooseConfig" returned a null meaning that the visual
         * context is not available.
         * Try a couple of variations to see if they will work.
         */
        if( fgState.DisplayMode & GLUT_MULTISAMPLE )
        {
            fgState.DisplayMode &= ~GLUT_MULTISAMPLE ;
            fghChooseConfig( &window->Window.pContext.egl.Config );
            fgState.DisplayMode |= GLUT_MULTISAMPLE;
        }
    }

    FREEGLUT_INTERNAL_ERROR_EXIT( window->Window.pContext.egl.Config != NULL,
                                  "EGL configuration with necessary capabilities "
                                  "not found", "fgOpenWindow" );

    if( ! positionUse )
        x = y = -1; /* default window position */
    if( ! sizeUse )
        w = h = 300; /* default window size */

    /*  Create the cursor  */
   window->Window.pContext.cursor = wl_cursor_theme_get_cursor(
                                      fgDisplay.pDisplay.cursor_theme,
                                      "left_ptr" ); 
   window->Window.pContext.cursor_surface = wl_compositor_create_surface(
                                              fgDisplay.pDisplay.compositor );

    /*  Create the main surface  */
    window->Window.pContext.surface = wl_compositor_create_surface(
                                        fgDisplay.pDisplay.compositor );

    /*  Create the shell surface with respects to the parent/child tree  */
    window->Window.pContext.xdg_surface = xdg_wm_base_get_xdg_surface(
                                          fgDisplay.pDisplay.xdg_wm_base,
                                           window->Window.pContext.surface );
    xdg_surface_add_listener( window->Window.pContext.xdg_surface,
                              &fghXdgSurfaceListener, window );

    if ( !isSubWindow && !window->IsMenu ) // toplevel
    {
      window->Window.pContext.xdg_toplevel = xdg_surface_get_toplevel(window->Window.pContext.xdg_surface);
      if ( gameMode ) // fullscreen
      {
        xdg_toplevel_set_fullscreen(window->Window.pContext.xdg_toplevel, NULL);
        // Maybe should check if actually got fullscreened in ack_configure handler
        window->State.IsFullscreen = GL_TRUE;
      }
      if( title )
        xdg_toplevel_set_title( window->Window.pContext.xdg_toplevel, title );
    }
    else // popup / menu
    {
      window->Window.pContext.xdg_positioner = xdg_wm_base_create_positioner(fgDisplay.pDisplay.xdg_wm_base);
      xdg_positioner_set_size( window->Window.pContext.xdg_positioner, w, h );
      xdg_positioner_set_anchor_rect( window->Window.pContext.xdg_positioner, x, y, w, h );
      window->Window.pContext.xdg_popup = xdg_surface_get_popup( window->Window.pContext.xdg_surface,
                                                                 window->Parent->Window.pContext.xdg_surface,
                                                                 window->Window.pContext.xdg_positioner );
    }

    /*  Create the Wl_EGL_Window  */
    window->Window.Context = fghCreateNewContext( window );
    window->Window.pContext.egl_window = wl_egl_window_create( 
                                           window->Window.pContext.surface,
                                           w, h);
    window->Window.pContext.egl.Surface = eglCreateWindowSurface( 
                              fgDisplay.pDisplay.egl.Display,
                              window->Window.pContext.egl.Config,
                              (EGLNativeWindowType)window->Window.pContext.egl_window,
                              NULL );
    eglMakeCurrent( fgDisplay.pDisplay.egl.Display, window->Window.pContext.egl.Surface,
                    window->Window.pContext.egl.Surface, window->Window.Context );

    window->Window.pContext.pointer_button_pressed = GL_FALSE;

    wl_display_roundtrip( fgDisplay.pDisplay.display );
    wl_surface_commit(window->Window.pContext.surface);
}


/*
 * Request a window resize
 */
void fgPlatformReshapeWindow( SFG_Window *window, int width, int height )
{
    fghOnReshapeNotify(window, width, height, GL_FALSE);

    if( window->Window.pContext.egl_window )
      wl_egl_window_resize( window->Window.pContext.egl_window,
                            width, height, 0, 0 );
}


/*
 * Closes a window, destroying the frame and OpenGL context
 */
void fgPlatformCloseWindow( SFG_Window* window )
{
    fghPlatformCloseWindowEGL(window);

    if ( window->Window.pContext.egl_window )
      wl_egl_window_destroy( window->Window.pContext.egl_window );
    if ( window->Window.pContext.xdg_toplevel )
      xdg_toplevel_destroy( window->Window.pContext.xdg_toplevel );
    if ( window->Window.pContext.xdg_popup )
      xdg_popup_destroy( window->Window.pContext.xdg_popup );
    if ( window->Window.pContext.xdg_positioner )
      xdg_positioner_destroy( window->Window.pContext.xdg_positioner );
    if ( window->Window.pContext.xdg_surface )
      xdg_surface_destroy( window->Window.pContext.xdg_surface );
    if ( window->Window.pContext.surface )
      wl_surface_destroy( window->Window.pContext.surface );
    if ( window->Window.pContext.cursor_surface )
      wl_surface_destroy( window->Window.pContext.cursor_surface );
}


/*
 * This function re-creates the window assets if they
 * have been destroyed
 */
void fgPlatformShowWindow( SFG_Window *window )
{
    if ( ! window->Window.pContext.egl_window ||
         ! window->Window.pContext.xdg_surface ||
         ! window->Window.pContext.surface)
    {
        fgPlatformCloseWindow( window );
        fgPlatformOpenWindow( window, "", /* TODO : save the title for further use */
                              GL_TRUE, window->State.Xpos, window->State.Ypos,
                              GL_TRUE, window->State.Width, window->State.Height,
                              (GLboolean)(window->State.IsFullscreen ? GL_TRUE : GL_FALSE),
                              (GLboolean)(window->Parent ? GL_TRUE : GL_FALSE) );
    }
    else 
    {
    /*     TODO : support this once we start using xdg-shell
     *
     *     xdg_surface_present( window->Window.pContext.shsurface, 0 );
     *     INVOKE_WCB( *window, WindowStatus, ( GLUT_FULLY_RETAINED ) );
     *     window->State.Visible = GL_TRUE;
     */
        fgWarning( "glutShownWindow(): function unsupported for an already existing"
                                     " window under Wayland" );
    }
}

/*
 * This function hides the specified window
 */
void fgPlatformHideWindow( SFG_Window *window )
{
    fgPlatformIconifyWindow( window );
}

/*
 * Iconify the specified window (top-level windows only)
 */
void fgPlatformIconifyWindow( SFG_Window *window )
{
    /* TODO : support this once we start using xdg-shell
     *
     * xdg_surface_set_minimized( window->Window.pContext.shsurface );
     * INVOKE_WCB( *window, WindowStatus, ( GLUT_HIDDEN ) );
     * window->State.Visible = GL_FALSE;
     */
    fgWarning( "glutIconifyWindow(): function unsupported under Wayland" );
}

/*
 * Set the current window's title
 */
void fgPlatformGlutSetWindowTitle( const char* title )
{
    SFG_Window* win = fgStructure.CurrentWindow;
    xdg_toplevel_set_title( win->Window.pContext.xdg_toplevel, title );
}

/*
 * Set the current window's iconified title
 */
void fgPlatformGlutSetIconTitle( const char* title )
{
    fgPlatformGlutSetWindowTitle( title );
}

/*
 * Change the specified window's position
 */
void fgPlatformPositionWindow( SFG_Window *window, int x, int y )
{
    /* pointless under Wayland */
    fgWarning( "glutPositionWindow(): function unsupported under Wayland" );
}

/*
 * Lowers the specified window (by Z order change)
 */
void fgPlatformPushWindow( SFG_Window *window )
{
    /* pointless under Wayland */
    fgWarning( "glutPushWindow(): function unsupported under Wayland" );
}

/*
 * Raises the specified window (by Z order change)
 */
void fgPlatformPopWindow( SFG_Window *window )
{
    /* pointless under Wayland */
    fgWarning( "glutPopWindow(): function unsupported under Wayland" );
}

/*
 * Toggle the window's full screen state.
 */
void fgPlatformFullScreenToggle( SFG_Window *win )
{
    if(fghToggleFullscreen() != -1) {
        win->State.IsFullscreen = !win->State.IsFullscreen;
    }
}

