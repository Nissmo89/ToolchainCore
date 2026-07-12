#pragma once

#include <QtCore/qglobal.h>

#if defined(CCC_STATIC)
#    define CCC_EXPORT
#elif defined(CCC_LIBRARY)
#    define CCC_EXPORT Q_DECL_EXPORT
#else
#    define CCC_EXPORT Q_DECL_IMPORT
#endif

