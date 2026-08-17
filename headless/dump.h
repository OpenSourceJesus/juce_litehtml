#pragma once

#include "container.h"
#include "crust_compat.h"

namespace headless {

// All of these append to a caller-supplied string rather than returning one.
// The subset allows returning a bare local by value, but building the result
// needed a stream, and appending through a pointer avoids both questions.

/** Dumps the laid-out element tree: tag name, display type and box geometry. */
void dumpLayoutTree (litehtml::document::ptr doc, std::string* out);

/** Dumps the recorded display list in paint order. */
void dumpDisplayList (Container* container, std::string* out);

/** Reconstructs the visible text from the display list, one output line per
    rendered line box. The closest thing the headless build has to "what the
    page looks like".
 */
void dumpText (Container* container, std::string* out);

/** Renders the display list into a coarse ASCII grid, for eyeballing block
    layout without a graphical backend.
 */
void dumpAscii (Container* container, int pageWidth, int pageHeight, std::string* out);

} // namespace headless
