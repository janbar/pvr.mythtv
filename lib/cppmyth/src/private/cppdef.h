#pragma once

#define SAFE_DELETE(p)        if ((p) != nullptr) { delete (p);    (p) = nullptr; }
#define SAFE_DELETE_ARRAY(p)  if ((p) != nullptr) { delete[] (p);  (p) = nullptr; }
