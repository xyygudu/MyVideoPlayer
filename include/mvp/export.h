
#ifndef MVP_CORE_EXPORT_H
#define MVP_CORE_EXPORT_H

#ifdef MVP_CORE_STATIC_DEFINE
#  define MVP_CORE_EXPORT
#  define MVP_CORE_NO_EXPORT
#else
#  ifndef MVP_CORE_EXPORT
#    ifdef mvp_core_EXPORTS
        /* We are building this library */
#      define MVP_CORE_EXPORT __declspec(dllexport)
#    else
        /* We are using this library */
#      define MVP_CORE_EXPORT __declspec(dllimport)
#    endif
#  endif

#  ifndef MVP_CORE_NO_EXPORT
#    define MVP_CORE_NO_EXPORT 
#  endif
#endif

#ifndef MVP_CORE_DEPRECATED
#  define MVP_CORE_DEPRECATED __declspec(deprecated)
#endif

#ifndef MVP_CORE_DEPRECATED_EXPORT
#  define MVP_CORE_DEPRECATED_EXPORT MVP_CORE_EXPORT MVP_CORE_DEPRECATED
#endif

#ifndef MVP_CORE_DEPRECATED_NO_EXPORT
#  define MVP_CORE_DEPRECATED_NO_EXPORT MVP_CORE_NO_EXPORT MVP_CORE_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef MVP_CORE_NO_DEPRECATED
#    define MVP_CORE_NO_DEPRECATED
#  endif
#endif

#endif /* MVP_CORE_EXPORT_H */
