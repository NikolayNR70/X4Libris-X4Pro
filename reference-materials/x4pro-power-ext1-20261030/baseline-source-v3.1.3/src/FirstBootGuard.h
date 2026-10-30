#pragma once
namespace FirstBootGuard {
void begin();
void observeHealthyLoop(unsigned long now, bool ready);
bool active();
bool hasFallback();
bool accept();
void rollback();
bool prepareUpdate();
}
