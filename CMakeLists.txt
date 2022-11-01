project(mocto-idf)

cmake_minimum_required(VERSION 3.12)

include_directories(
    include/
)

install(DIRECTORY include DESTINATION /usr)
