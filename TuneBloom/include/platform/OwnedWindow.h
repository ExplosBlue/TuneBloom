#pragma once

namespace ownedwindow {

void prepare(void* window);
void attach(void* window, void* owner);
void detach(void* window);

}
