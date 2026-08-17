#pragma once

// The headless sources are written in the Crust C++ subset (see CPPRUST.md)
// so they can be lowered to C by tools/cpprust.py. They also have to build
// with an ordinary C++ compiler, and the two disagree on exactly one name.
//
// Crust supplies `std::ownvector<T>` for element types that own something --
// a separate template from `vector<T>` because the two need different
// parameter conventions. Real STL has no such name, so provide it there.
//
// cpprust.py decides #ifndef while splicing, so build it with -D CRUST and
// this block never reaches the lowering.

#include <string>
#include <vector>

#ifndef CRUST
namespace std {
    template<typename T> using ownvector = vector<T>;
}
#endif
