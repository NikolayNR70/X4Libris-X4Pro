#include <Fb2Converter.h>

#include <iostream>
int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--source-path") {
    std::cout << Fb2Converter::sourcePath(argv[2]);
    return 0;
  }
  if (argc == 3 && std::string(argv[1]) == "--reader-cache") {
    Fb2Converter::ReaderBook book;
    std::string error;
    if (!Fb2Converter::prepareReaderCache(argv[2], book, error)) {
      std::cerr << error;
      return 1;
    }
    std::cout << book.cacheDir << "\n" << book.title << "\n" << book.author << "\n" << book.language << "\n"
              << book.chapters.size() << "\n" << book.images.size();
    return 0;
  }
  if (argc == 3 && std::string(argv[1]) == "--reader-paths") {
    Fb2Converter::ReaderBook book;
    std::string error;
    if (!Fb2Converter::prepareReaderCache(argv[2], book, error)) {
      std::cerr << error;
      return 1;
    }
    for (const auto& chapter : book.chapters) std::cout << chapter.sourceXPath << "\n";
    return 0;
  }
  if (argc != 2) return 2;
  std::string output, error;
  if (!Fb2Converter::convert(argv[1], output, error)) {
    std::cerr << error;
    return 1;
  }
  std::cout << output << "\n" << Fb2Converter::sourcePath(output);
  return 0;
}
