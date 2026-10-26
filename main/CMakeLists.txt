idf_component_register(
    SRCS "main.c"
         # FoloTalker 应用 —— 纯逻辑模块(主机可测)
         "ft_protocol.c"
         "ft_state.c"
         "ft_store.c"
         "ft_textfield.c"
         "ft_form.c"
         "ft_glyphs.c"
         "ft_texts.c"
         # FoloTalker 应用 —— 设备相关模块
         "ft_app.c"
         "ft_net.c"
         "ft_audio.c"
         "ft_prov.c"
         "ft_ui.c"
         "ft_theme.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi
              nvs_flash esp-tls mbedtls esp_websocket_client
)

# 生成的中文字体子集。放在 assets/fonts/ 下,随仓库一起版本控制。
# 只在这里注册一次,不要在别的源文件里 #include 它们的 .c。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ft_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ft_font_20.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ft_font_28.c"
)
