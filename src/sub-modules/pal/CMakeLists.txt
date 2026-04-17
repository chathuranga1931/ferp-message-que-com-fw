# pal — Platform Abstraction Layer (headers only IDF component)
#
# Exposes all PAL headers to any component that lists `pal` in its
# REQUIRES or PRIV_REQUIRES.  Platform-specific implementations
# (esp-idf/, mac-pc/, arduino/) are compiled by the product layer.

idf_component_register(
    INCLUDE_DIRS "."
)
