/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_COMPOSITOR_CLUTTER_PLUGIN_H_
#define META_COMPOSITOR_CLUTTER_PLUGIN_H_

#include "types.h"
#include "config.h"
#include "compositor.h"

#include <clutter/clutter.h>

/*
 * This file defines the plugin API.
 *
 * Effects plugin is shared library loaded via dlopen(); it is recommended
 * that the GModule API is used (otherwise you are on your own to do proper
 * plugin clean up when the module is unloaded).
 *
 * The plugin interface is exported via the MetaCompositorClutterPlugin struct.
 */

/*
 * Alias MetaRectangle to PluginWorkspaceRectangle in anticipation of
 * making this file metacity-independent (we want the plugins to be portable
 * between different WMs.
 */
typedef MetaRectangle PluginWorkspaceRectangle;

/*
 * The name of the header struct; use as:
 *
 * MetaCompositorClutterPlugin META_COMPOSITOR_CLUTTER_PLUGIN_STRUCT =
 *   {
 *     ...
 *   };
 *
 * See clutter-plugins/simple.c for example code.
 */
#define META_COMPOSITOR_CLUTTER_PLUGIN_STRUCT MCCPS__

/*
 * Definition for the plugin init function; use as:
 *
 *   META_COMPOSITOR_CLUTTER_PLUGIN_INIT_FUNC
 *   {
 *     init code ...
 *   }
 *
 * See clutter-plugins/simple.c for example code.
 */
#define META_COMPOSITOR_CLUTTER_PLUGIN_INIT_FUNC \
  gboolean mccp_init__(void);                    \
  gboolean mccp_init__()


/* Private; must match the above */
#define META_COMPOSITOR_CLUTTER_PLUGIN_STRUCT_NAME    "MCCPS__"
#define META_COMPOSITOR_CLUTTER_PLUGIN_INIT_FUNC_NAME "mccp_init__"

typedef struct MetaCompositorClutterPlugin MetaCompositorClutterPlugin;

/*
 * Feature flags: identify events that the plugin can handle; a plugin can
 * handle one or more events.
 */
#define META_COMPOSITOR_CLUTTER_PLUGIN_MINIMIZE         0x00000001UL
#define META_COMPOSITOR_CLUTTER_PLUGIN_MAXIMIZE         0x00000002UL
#define META_COMPOSITOR_CLUTTER_PLUGIN_UNMAXIMIZE       0x00000004UL
#define META_COMPOSITOR_CLUTTER_PLUGIN_MAP              0x00000008UL
#define META_COMPOSITOR_CLUTTER_PLUGIN_DESTROY          0x00000010UL
#define META_COMPOSITOR_CLUTTER_PLUGIN_SWITCH_WORKSPACE 0x00000020UL

#define META_COMPOSITOR_CLUTTER_PLUGIN_ALL_EFFECTS      0xffffffffUL

/*
 * A key that the switch_workspace() handler can use to retrive workspace
 * the actor is on.
 */
#define META_COMPOSITOR_CLUTTER_PLUGIN_WORKSPACE_KEY "MCCP-Manager-workspace"

struct MetaCompositorClutterPlugin
{
  /*
   * Version information; the first three numbers match the Metacity version
   * with which the plugin was compiled (see clutter-plugins/simple.c for sample
   * code).
   */
  guint version_major;
  guint version_minor;
  guint version_micro;

  /*
   * Version of the plugin API; this is unrelated to the matacity version
   * per se. The API version is checked by the plugin manager and must match
   * the one used by it (see clutter-plugins/simple.c for sample code).
   */
  guint version_api;

#ifndef META_COMPOSITOR_CLUTTER_BUILDING_PLUGIN
  const
#endif
  gchar   *name;     /* Human-readable name for UI */
  gulong   features; /* or-ed feature flags */

  /*
   * Event handlers
   *
   * Plugins must not make any special assumptions about the nature of
   * ClutterActor, as the implementation details can change.
   *
   * Plugins must restore actor properties on completion (i.e., fade effects
   * must restore opacity back to the original value, scale effects scale,
   * etc.).
   *
   * On completion, each event handler must call the manager completed()
   * callback function.
   */
  void (*minimize)         (ClutterActor       *actor,
                            MetaCompWindowType  type,
                            gint                workspace);

  void (*maximize)         (ClutterActor       *actor,
                            MetaCompWindowType  type,
                            gint                workspace,
                            gint                x,
                            gint                y,
                            gint                width,
                            gint                height);

  void (*unmaximize)       (ClutterActor       *actor,
                            MetaCompWindowType  type,
                            gint                workspace,
                            gint                x,
                            gint                y,
                            gint                width,
                            gint                height);

  void (*map)              (ClutterActor       *actor,
                            MetaCompWindowType  type,
                            gint                workspace);

  void (*destroy)          (ClutterActor       *actor,
                            MetaCompWindowType  type,
                            gint                workspace);

  /*
   * Each actor in the list has a workspace number attached to it using
   * g_object_set_data() with key META_COMPOSITOR_CLUTTER_PLUGIN_WORKSPACE_KEY;
   * workspace < 0 indicates the window is sticky (i.e., on all desktops).
   */
  void (*switch_workspace) (const GList       **actors,
                            gint                from,
                            gint                to);

  /*
   * Called if an effect should be killed prematurely; the plugin must
   * call the completed() callback as if the effect terminated naturally.
   * The events parameter is a bitmask indicating which effects are to be
   * killed.
   */
  void (*kill_effect)      (ClutterActor       *actor,
                            gulong              events);


  /*
   * The plugin manager will call this function when module should be reloaded.
   * This happens, for example, when the parameters for the plugin changed.
   */
  gboolean (*reload) (void);

#ifdef META_COMPOSITOR_CLUTTER_BUILDING_PLUGIN
  const
#endif
  gchar *params;  /* String containing additional parameters for the plugin;
                   * this is specified after the pluing name in the gconf
                   * database, separated by a colon.
                   *
                   * The following parameter tokens need to be handled by all
                   * plugins:
                   *
                   *   'debug'
                   *             Indicates running in debug mode; the plugin
                   *             might want to print useful debug info, or
                   *             extend effect duration, etc.
                   *
                   *   'disable: ...;'
                   *
                   *             The disable token indicates that the effects
                   *             listed after the colon should be disabled.
                   *
                   *             The list is comma-separated, terminated by a
                   *             semicolon and consisting of the following
                   *             tokens:
                   *
                   *                minimize
                   *                maximize
                   *                unmaximize
                   *                map
                   *                destroy
                   *                switch-workspace
                   */

  ClutterActor *stage;

  gint   screen_width;
  gint   screen_height;

  GList *work_areas; /* List of PluginWorkspaceRectangles defining the
                      * geometry of individual workspaces.
                      */

  gint   running; /* Plugin must increase this counter for each effect it starts
                   * decrease it again once the effect finishes.
                   */

  void  *plugin_private; /* Plugin private data go here; use the plugin init
                          * function to allocate and initialize any private
                          * data.
                          */

  /*
   * Manager callback for completed effects; this function must be called on the
   * completion of each effect.
   *
   * For switch-workspace effect the plugin might pass back any actor from the
   * actor list, but the actor parameter must not be NULL.
   */
  void (*completed) (MetaCompositorClutterPlugin *plugin,
                     ClutterActor                *actor,
                     unsigned long                event);

  /* Private; manager private data. */
  void *manager_private;
};

#endif