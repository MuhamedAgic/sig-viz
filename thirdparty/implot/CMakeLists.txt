add_library(implot STATIC
	implot/implot.cpp
	implot/implot_items.cpp
	implot/implot_demo.cpp
)
target_include_directories(implot PUBLIC implot)
target_link_libraries(implot PUBLIC imgui)
