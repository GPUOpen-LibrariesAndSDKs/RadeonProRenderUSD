install(
    FILES rpr.py
    DESTINATION lib/python/rprUsdviewMenu
    RENAME "__init__.py")
install(
    FILES plugInfo.json
    DESTINATION plugin/usd/rprUsdviewMenu/resources)
