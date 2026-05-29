sed -i '1085d' src/BookDatabase.cpp
cat << 'FMT' > format.sh
find src \( -name '*.cpp' -o -name '*.cc' -o -name '*.h' -o -name '*.hpp' \) -print0 | xargs -0 clang-format -i
FMT
bash format.sh
rm format.sh
git add src/
