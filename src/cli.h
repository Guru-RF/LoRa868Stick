#pragma once

void cliBegin();
void cliEnter();   // called on Ctrl+]
void cliTick();    // call each loop iteration
bool cliActive();  // true when CLI prompt is active
