/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "Toast Engine", "index.html", [
    [ "Building", "index.html#autotoc_md101", [
      [ "Prerequisites", "index.html#autotoc_md102", null ],
      [ "Generation", "index.html#autotoc_md103", null ],
      [ "Build", "index.html#autotoc_md104", null ],
      [ "Tests", "index.html#autotoc_md105", null ]
    ] ],
    [ "Asset types specs", "md_docs_2asset__specs.html", null ],
    [ "Event System", "md_docs_2event__system.html", [
      [ "Core Concepts", "md_docs_2event__system.html#autotoc_md2", [
        [ "Event Definition", "md_docs_2event__system.html#autotoc_md3", null ],
        [ "Event Sending", "md_docs_2event__system.html#autotoc_md4", null ],
        [ "Event Callbacks", "md_docs_2event__system.html#autotoc_md5", null ],
        [ "Subscribing / Unsubscribing Callbacks", "md_docs_2event__system.html#autotoc_md6", null ],
        [ "Dispatching Events", "md_docs_2event__system.html#autotoc_md7", null ]
      ] ],
      [ "Implementation", "md_docs_2event__system.html#autotoc_md8", [
        [ "Event Definition", "md_docs_2event__system.html#autotoc_md9", [
          [ "IEvent", "md_docs_2event__system.html#autotoc_md10", null ],
          [ "Event&lt;T&gt;", "md_docs_2event__system.html#autotoc_md11", [
            [ "Storing Callbacks", "md_docs_2event__system.html#autotoc_md12", null ]
          ] ],
          [ "Subscribing / Unsubscribing Callbacks", "md_docs_2event__system.html#autotoc_md13", null ]
        ] ],
        [ "Listener", "md_docs_2event__system.html#autotoc_md14", null ],
        [ "Sending Events", "md_docs_2event__system.html#autotoc_md15", null ],
        [ "Dispatching Events", "md_docs_2event__system.html#autotoc_md16", [
          [ "<span class=\"tt\">notify()</span>", "md_docs_2event__system.html#autotoc_md17", null ]
        ] ]
      ] ],
      [ "Performance", "md_docs_2event__system.html#autotoc_md18", null ],
      [ "Expansion", "md_docs_2event__system.html#autotoc_md19", null ],
      [ "Changelog", "md_docs_2event__system.html#autotoc_md20", null ]
    ] ],
    [ "Logging system", "md_docs_2logging__system.html", [
      [ "Motivation", "md_docs_2logging__system.html#autotoc_md22", null ],
      [ "Uses", "md_docs_2logging__system.html#autotoc_md23", [
        [ "Initialization", "md_docs_2logging__system.html#autotoc_md24", null ],
        [ "Logging a message", "md_docs_2logging__system.html#autotoc_md25", null ],
        [ "Using Kenzo", "md_docs_2logging__system.html#autotoc_md26", null ]
      ] ],
      [ "Performance", "md_docs_2logging__system.html#autotoc_md27", null ],
      [ "Expansion", "md_docs_2logging__system.html#autotoc_md28", null ]
    ] ],
    [ "NAME OF PROJECT", "md_docs_2motivation__template.html", [
      [ "Motivation", "md_docs_2motivation__template.html#autotoc_md30", null ],
      [ "Uses", "md_docs_2motivation__template.html#autotoc_md31", null ],
      [ "Performance", "md_docs_2motivation__template.html#autotoc_md32", null ],
      [ "Unit tests", "md_docs_2motivation__template.html#autotoc_md33", null ],
      [ "Expansion", "md_docs_2motivation__template.html#autotoc_md34", null ],
      [ "Changelog", "md_docs_2motivation__template.html#autotoc_md35", null ]
    ] ],
    [ "Prefabs", "md_docs_2prefabs.html", [
      [ "Loading a prefab", "md_docs_2prefabs.html#autotoc_md37", null ],
      [ "Prefab instances", "md_docs_2prefabs.html#autotoc_md38", null ],
      [ "Overrides", "md_docs_2prefabs.html#autotoc_md39", null ],
      [ "Self-referencing prefabs", "md_docs_2prefabs.html#autotoc_md40", null ],
      [ "Saving", "md_docs_2prefabs.html#autotoc_md41", null ]
    ] ],
    [ "Reflection", "md_docs_2reflection.html", [
      [ "Allowed attributes", "md_docs_2reflection.html#autotoc_md43", [
        [ "Node level attributes", "md_docs_2reflection.html#autotoc_md44", null ],
        [ "Field level attributes", "md_docs_2reflection.html#autotoc_md45", null ],
        [ "Function level attributes", "md_docs_2reflection.html#autotoc_md46", null ]
      ] ],
      [ "Reflecting enum fields", "md_docs_2reflection.html#autotoc_md47", null ],
      [ "Reflecting functions", "md_docs_2reflection.html#autotoc_md48", null ],
      [ "Motivation", "md_docs_2reflection.html#autotoc_md49", null ]
    ] ],
    [ "Vulkan Renderer", "md_docs_2renderer.html", [
      [ "Motivation", "md_docs_2renderer.html#autotoc_md51", null ],
      [ "Uses", "md_docs_2renderer.html#autotoc_md52", [
        [ "Creating the renderer", "md_docs_2renderer.html#autotoc_md53", null ],
        [ "Submitting frames", "md_docs_2renderer.html#autotoc_md54", null ],
        [ "Resource uploads", "md_docs_2renderer.html#autotoc_md55", null ],
        [ "Output targets", "md_docs_2renderer.html#autotoc_md56", null ],
        [ "Resizing", "md_docs_2renderer.html#autotoc_md57", null ]
      ] ],
      [ "Performance", "md_docs_2renderer.html#autotoc_md58", null ],
      [ "Expansion", "md_docs_2renderer.html#autotoc_md59", null ],
      [ "Shaders and materials", "md_docs_2renderer.html#autotoc_md60", null ],
      [ "Known issues", "md_docs_2renderer.html#autotoc_md61", null ]
    ] ],
    [ "Lua Scripting", "md_docs_2scripting.html", [
      [ "Motivation", "md_docs_2scripting.html#autotoc_md63", null ],
      [ "Uses", "md_docs_2scripting.html#autotoc_md64", [
        [ "Anatomy of a script", "md_docs_2scripting.html#autotoc_md65", null ],
        [ "Inspector variables", "md_docs_2scripting.html#autotoc_md66", null ],
        [ "Talking to nodes", "md_docs_2scripting.html#autotoc_md67", null ],
        [ "Hot reload", "md_docs_2scripting.html#autotoc_md68", null ],
        [ "Editor completion", "md_docs_2scripting.html#autotoc_md69", null ],
        [ "Logging and profiling", "md_docs_2scripting.html#autotoc_md70", null ]
      ] ],
      [ "Performance", "md_docs_2scripting.html#autotoc_md71", [
        [ "Physics from scripts", "md_docs_2scripting.html#autotoc_md72", null ],
        [ "Building a prefab", "md_docs_2scripting.html#autotoc_md73", null ],
        [ "init, begin and onEnable", "md_docs_2scripting.html#autotoc_md74", null ]
      ] ],
      [ "Unit tests", "md_docs_2scripting.html#autotoc_md75", null ],
      [ "Expansion", "md_docs_2scripting.html#autotoc_md76", null ]
    ] ],
    [ "In-Game UI", "md_docs_2ui.html", [
      [ "Motivation", "md_docs_2ui.html#autotoc_md78", null ],
      [ "Uses", "md_docs_2ui.html#autotoc_md79", [
        [ "Authoring documents", "md_docs_2ui.html#autotoc_md80", null ],
        [ "Panels", "md_docs_2ui.html#autotoc_md81", null ],
        [ "Lua events and binds", "md_docs_2ui.html#autotoc_md82", null ],
        [ "Localization and <span class=\"tt\">${}</span> formatting", "md_docs_2ui.html#autotoc_md83", null ],
        [ "Rendering effects", "md_docs_2ui.html#autotoc_md84", null ]
      ] ],
      [ "Performance", "md_docs_2ui.html#autotoc_md85", null ],
      [ "Unit tests", "md_docs_2ui.html#autotoc_md86", null ],
      [ "Expansion", "md_docs_2ui.html#autotoc_md87", null ],
      [ "Changelog", "md_docs_2ui.html#autotoc_md88", null ]
    ] ],
    [ "World", "md_docs_2world.html", [
      [ "Nodes and the scene graph", "md_docs_2world.html#autotoc_md90", [
        [ "Node states", "md_docs_2world.html#autotoc_md91", null ],
        [ "Node types", "md_docs_2world.html#autotoc_md92", null ]
      ] ],
      [ "Loading and switching scenes", "md_docs_2world.html#autotoc_md93", null ],
      [ "Searching the tree", "md_docs_2world.html#autotoc_md94", null ],
      [ "Dependency graph", "md_docs_2world.html#autotoc_md95", null ],
      [ "Tick phases and wave execution", "md_docs_2world.html#autotoc_md96", null ],
      [ "Lifecycle", "md_docs_2world.html#autotoc_md97", null ],
      [ "Workspace", "md_docs_2world.html#autotoc_md98", null ]
    ] ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", null ],
        [ "Functions", "namespacemembers_func.html", null ],
        [ "Variables", "namespacemembers_vars.html", null ],
        [ "Typedefs", "namespacemembers_type.html", null ],
        [ "Enumerations", "namespacemembers_enum.html", null ]
      ] ]
    ] ],
    [ "Concepts", "concepts.html", "concepts" ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", "functions_vars" ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Enumerations", "functions_enum.html", null ],
        [ "Properties", "functions_prop.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Functions", "globals_func.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"aabb_8hpp.html",
"classassets_1_1Volume.html#ae8fb61d3ee89c914f68fbf7e21f636da",
"classeditor_1_1Assets_1_1Types_1_1VoxelModelAsset.html",
"classeditor_1_1Git_1_1CommitView.html",
"classeditor_1_1Workspace_1_1HierarchyElement.html",
"classphysics_1_1Simulator.html#a722e5c202b888caef369974d5a2ee12b",
"classrenderer_1_1SceneDescriptorSets.html",
"classrenderer_1_1VulkanRenderer.html#a322b99046a09baf3ddb3615e11c12507",
"classtoast_1_1AudioEmitter.html#a76ef346d2a66e40abbb43ddd9e6ddfed",
"classtoast_1_1Node.html#ae37c89e7fd2eaeed46bea58784bb8ad5",
"classtoast_1_1VoxelNode.html#a047104df879c6d7eba36646c7c0331a3",
"classtoast_1_1Workspace.html#a580beb85b06c63ceee52c4df9f101969",
"environment__pass_8hpp_source.html",
"layers_8hpp.html",
"namespacerenderer.html#a9edc6f6486f8dbb9b1ed51ddb3c8e925",
"structassets_1_1AnimationClip.html#a503599763cd5aee195d5e3b9d1494070",
"structevent_1_1ProtoTraits_3_01WindowMousePosition_01_4.html",
"structphysics_1_1QuerySnapshot.html",
"structrenderer_1_1Meta.html#a581b3f5775b0c563478d97efd7b6bb8b",
"structrenderer_1_1sync_1_1ImageState.html",
"structtoast_1_1Workspace_1_1VoxelToolState.html#a9a4ac7d1868da1be2ab383525d812de4",
"volumetric__pass_8hpp_source.html"
];

const SYNCONMSG = 'click to disable panel synchronization';
const SYNCOFFMSG = 'click to enable panel synchronization';
const LISTOFALLMEMBERS = 'List of all members';