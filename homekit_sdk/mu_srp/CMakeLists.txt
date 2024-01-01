set(COMPONENT_ADD_INCLUDEDIRS .)

# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES mbedtls)  # mu_bignum.h includes mbedtls/bignum.h (public)
set(COMPONENT_PRIV_REQUIRES hkdf-sha)

set(COMPONENT_SRCS ./mu_srp.c)

register_component()
