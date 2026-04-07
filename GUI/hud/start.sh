rm -rf build
cmake -S . -B build -G Ninja
cmake --build build
cd ./build
./hud
echo $?