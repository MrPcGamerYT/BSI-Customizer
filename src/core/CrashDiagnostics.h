#pragma once

// Minimal, local-only Windows exception record. Never captures memory, OAuth
// state, user sessions, network messages or file contents.
void installCrashDiagnostics();
