#pragma once

namespace toast {
// App provides this factory function (in your TestApp).
// The engine's entry-point will call this to obtain the application instance.
// Signature must match what Entrypoint implementation expects.
class Engine;                   // forward declare to avoid including Engine.h in this header
Engine* CreateApplication();    // NOLINT
}
