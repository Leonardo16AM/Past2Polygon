#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <stack>
#include <algorithm>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Color structure
struct Color {
    unsigned char r, g, b;
};

// Global configuration
struct Config {
    double k;
    bool use8Way;
    bool euclidif;
    bool adj;
    int minComponentSize;
    double buildingBlockTreshold;
};

// Function to calculate color difference
double colorDifference(const Color& c1, const Color& c2, bool euclidif) {
    if (euclidif) {
        return std::sqrt(std::pow(c1.r - c2.r, 2) +
                         std::pow(c1.g - c2.g, 2) +
                         std::pow(c1.b - c2.b, 2));
    } else {
        return std::abs(c1.r - c2.r) +
               std::abs(c1.g - c2.g) +
               std::abs(c1.b - c2.b);
    }
}

int currentComponentXmin, currentComponentXmax, currentComponentYmin, currentComponentYmax, currentComponentSize;

// Modified Flood Fill Algorithm
void floodFillIterative(std::vector<Color>& image, int startX, int startY, int width, int height,
                        std::vector<bool>& visited, const Color& startColor, double k,
                        bool use8Way, bool adj, bool euclidif, const Color& newColor, std::vector<bool>& bigMask) {
    std::stack<std::tuple<int, int, Color>> stack;
    stack.push(std::make_tuple(startX, startY, startColor));

    while (!stack.empty()) {
        int x = std::get<0>(stack.top());
        int y = std::get<1>(stack.top());
        Color neighborColor = std::get<2>(stack.top());
        stack.pop();

        if (x < 0 || x >= width || y < 0 || y >= height || visited[y * width + x]) {
            continue;
        }

        currentComponentXmin = std::min(currentComponentXmin, x);
        currentComponentXmax = std::max(currentComponentXmax, x);
        currentComponentYmin = std::min(currentComponentYmin, y);
        currentComponentYmax = std::max(currentComponentYmax, y);
        currentComponentSize++;

        visited[y * width + x] = true;
        bigMask[y * width + x] = true;

        Color currentColor = image[y * width + x];
        Color compareColor = adj ? neighborColor : startColor; // Use neighbor's color if adj is true

        if (colorDifference(currentColor, compareColor, euclidif) <= k) {
            image[y * width + x] = newColor;

            stack.push(std::make_tuple(x + 1, y, currentColor));
            stack.push(std::make_tuple(x - 1, y, currentColor));
            stack.push(std::make_tuple(x, y + 1, currentColor));
            stack.push(std::make_tuple(x, y - 1, currentColor));

            if (use8Way) {
                stack.push(std::make_tuple(x + 1, y + 1, currentColor));
                stack.push(std::make_tuple(x + 1, y - 1, currentColor));
                stack.push(std::make_tuple(x - 1, y + 1, currentColor));
                stack.push(std::make_tuple(x - 1, y - 1, currentColor));
            }
        }
    }
}

// Function to read configuration
Config readConfig(const std::string& configFile) {
    Config config;
    std::ifstream file(configFile);
    if (!file) {
        throw std::runtime_error("Could not open config file.");
    }

    file >> config.k >> config.use8Way >> config.euclidif >> config.adj >> config.minComponentSize >> config.buildingBlockTreshold;
    file.close();
    return config;
}

// Function to get files in a directory
std::vector<std::string> getFiles(const std::string& directory) {
    std::vector<std::string> files;
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        throw std::runtime_error("Could not open directory: " + directory);
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name != "." && name != "..") {
            files.push_back(directory + "/" + name);
        }
    }
    closedir(dir);
    return files;
}

// Function to create a directory
bool createDirectory(const std::string& dir) {
    #ifdef _WIN32
    return mkdir(dir.c_str()) == 0 || errno == EEXIST; // Windows
    #else
    return mkdir(dir.c_str(), 0777) == 0 || errno == EEXIST; // POSIX
    #endif
}

// Function to save the segmentation image
void saveSegmentation(const std::vector<Color>& image, int width, int height, const std::string& outputPath) {
    unsigned char* outputData = new unsigned char[width * height * 3];
    for (int j = 0; j < width * height; ++j) {
        outputData[j * 3] = image[j].r;
        outputData[j * 3 + 1] = image[j].g;
        outputData[j * 3 + 2] = image[j].b;
    }
    stbi_write_jpg(outputPath.c_str(), width, height, 3, outputData, 100);
    delete[] outputData;
}

// Function to save a mask for a connected component
void saveMask(const std::vector<bool>& mask, int width, int height, const std::string& filePath) {
    std::vector<unsigned char> maskImage(width * height * 3, 255);
    for (int i = 0; i < width * height; ++i) {
        if (mask[i]) {
            maskImage[i * 3] = 0;     // Black pixel for component
            maskImage[i * 3 + 1] = 0;
            maskImage[i * 3 + 2] = 0;
        }
    }
    stbi_write_jpg(filePath.c_str(), width, height, 3, maskImage.data(), 100);
}

// Helper function to escape characters in a string
std::string escapeJsonString(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

void processImages(const std::string& inputDir, const std::string& outputDir, const Config& config) {
    std::vector<std::string> files = getFiles(inputDir);
    std::cout << "Number of files in the directory: " << files.size() << "\n";

    int i = -1;
    for (const auto& filePath : files) {
        std::cout << filePath <<"\n";
        // Check if the file is an image based on its extension
        if (filePath.size() >= 4 && filePath.substr(filePath.size() - 4) == ".hmp") continue;
        i++;

        // Load image data
        int width, height, channels;
        unsigned char* imgData = stbi_load(filePath.c_str(), &width, &height, &channels, 3);
        if (!imgData) {
            std::cerr << "Failed to load image: " << filePath << "\n";
            continue;
        }

        std::cout << "Processing image " << i + 1 << ": " << filePath
                  << " (Width: " << width << ", Height: " << height << ", Channels: " << channels << ")\n";

        // Derive the heatmap file path
        std::string heatmapPath = filePath.substr(0, filePath.size() - 4) + ".hmp";

        // Open and read the heatmap file
        std::ifstream heatmapFile(heatmapPath, std::ios::binary);
        if (!heatmapFile) {
            std::cerr << "Failed to load heatmap file: " << heatmapPath << "\n";
            stbi_image_free(imgData);
            continue;
        }

        std::vector<float> heatmap(width * height);
        heatmapFile.read(reinterpret_cast<char*>(heatmap.data()), width * height * sizeof(float));
        if (!heatmapFile) {
            std::cerr << "Error reading heatmap data from file: " << heatmapPath << "\n";
            stbi_image_free(imgData);
            continue;
        }
        heatmapFile.close();

        std::vector<Color> image(width * height);
        std::vector<Color> buildingBlocksImage(width * height);
        for (int j = 0; j < width * height; ++j) {
            image[j] = {imgData[j * 3], imgData[j * 3 + 1], imgData[j * 3 + 2]};
            buildingBlocksImage[j] = {255, 255, 255};
        }
        stbi_image_free(imgData);

        std::ostringstream folderPath;
        folderPath << outputDir << "/" << std::setw(3) << std::setfill('0') << (i + 1);
        if (!createDirectory(folderPath.str())) {
            std::cerr << "Failed to create directory: " << folderPath.str() << "\n";
            continue;
        }
        std::ostringstream buildingBlocksFolderPath;
        buildingBlocksFolderPath << folderPath.str() << "/building_blocks";
        if (!createDirectory(buildingBlocksFolderPath.str())) {
            std::cerr << "Failed to create directory: " << buildingBlocksFolderPath.str() << "\n";
            continue;
        }
        std::ostringstream nonBuildingBlocksFolderPath;
        nonBuildingBlocksFolderPath << folderPath.str() << "/non_building_blocks";
        if (!createDirectory(nonBuildingBlocksFolderPath.str())) {
            std::cerr << "Failed to create directory: " << nonBuildingBlocksFolderPath.str() << "\n";
            continue;
        }

        // Open a file to store component information in JSON format
        std::ofstream componentInfoFile(folderPath.str() + "/components_info.json");
        if (!componentInfoFile.is_open()) {
           std::cerr << "Failed to create components_info.json\n";
           continue;
        }
        // Start JSON array
        componentInfoFile << "[\n";

        std::vector<bool> visited(width * height, false);
        std::vector<bool> bigMask(width * height, false);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, 255);

        auto start = std::chrono::high_resolution_clock::now();

        int componentCount = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!visited[y * width + x]) {
                    Color newColor = {static_cast<unsigned char>(distrib(gen)),
                                      static_cast<unsigned char>(distrib(gen)),
                                      static_cast<unsigned char>(distrib(gen))};

                    currentComponentXmin = 1000000000;
                    currentComponentXmax = -1000000000;
                    currentComponentYmin = 1000000000;
                    currentComponentYmax = -1000000000;
                    currentComponentSize = 0;

                    floodFillIterative(image, x, y, width, height, visited, image[y * width + x],
                                       config.k, config.use8Way, config.adj, config.euclidif, newColor, bigMask);

                    int componentWidth = currentComponentXmax - currentComponentXmin + 1;
                    int componentHeight = currentComponentYmax - currentComponentYmin + 1;

                    // Filter too-small components (try to keep only building blocks, heuristic)
                    if (currentComponentSize < config.minComponentSize || currentComponentSize < (componentWidth * componentHeight) / 3) {
                        for (int cx = currentComponentXmin; cx <= currentComponentXmax; cx++) {
                            for (int cy = currentComponentYmin; cy <= currentComponentYmax; cy++) {
                                bigMask[cy * width + cx] = false;
                            }
                        }
                        continue;
                    }

                    float heatmapThreshold = config.buildingBlockTreshold;

                    // Inside the component processing loop
                    float totalProbability = 0.0f;
                    int pixelCount = 0;

                    // Save mask for the component
                    std::vector<bool> mask(componentWidth * componentHeight, false);
                    for (int cx = currentComponentXmin; cx <= currentComponentXmax; cx++) {
                        for (int cy = currentComponentYmin; cy <= currentComponentYmax; cy++) {
                            if (bigMask[cy * width + cx]) {
                                mask[(cy - currentComponentYmin) * componentWidth + cx - currentComponentXmin] = true;
                                totalProbability += heatmap[cy * width + cx];
                                ++pixelCount;
                            }
                        }
                    }


                    float avgProbability = totalProbability / pixelCount;

                    for (int cx = currentComponentXmin; cx <= currentComponentXmax; cx++) {
                        for (int cy = currentComponentYmin; cy <= currentComponentYmax; cy++) {
                            if (bigMask[cy * width + cx]) {
                                bigMask[cy * width + cx] = false;
                                if(avgProbability >= heatmapThreshold && currentComponentSize < width*height/4)
                                    buildingBlocksImage[cy * width + cx] = {0,0,0};
                            }
                        }
                    }

                    // Save the component in the appropriate folder
                    std::ostringstream targetPath;
                    if (avgProbability >= heatmapThreshold) {
                        targetPath << buildingBlocksFolderPath.str() << "/component_" << std::setw(5) << std::setfill('0') << (componentCount + 1) << ".jpg";
                    } else {
                        targetPath << nonBuildingBlocksFolderPath.str() << "/component_" << std::setw(5) << std::setfill('0') << (componentCount + 1) << ".jpg";
                    }

                    saveMask(mask, componentWidth, componentHeight, targetPath.str());

                    // Write component information to the file

                    // Add a comma before every object from the second component
                    if (componentCount > 0) {
                        componentInfoFile << ",\n";
                    }
                    // Write component information as a JSON object
                    componentInfoFile << "  {\n";
                    componentInfoFile << "    \"component\": " << componentCount + 1 << ",\n";
                    componentInfoFile << "    \"topLeftCorner\": {\n";
                    componentInfoFile << "      \"x\": " << currentComponentXmin << ",\n";
                    componentInfoFile << "      \"y\": " << currentComponentYmin << "\n";
                    componentInfoFile << "    },\n";
                    componentInfoFile << "    \"width\": " << componentWidth << ",\n";
                    componentInfoFile << "    \"height\": " << componentHeight << ",\n";
                    componentInfoFile << "    \"buildingBlockProbability\": " << avgProbability << "\n";
                    componentInfoFile << "  }";

                    ++componentCount;
                    if(componentCount % 100 == 0) std::cout << componentCount << " processed components.\n";
                }
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;

        std::cout << "Finished processing: " << filePath
                  << " (Components: " << componentCount << ", Time: " << elapsed.count() << "s)\n";

        // Save segmentation image
        std::ostringstream segPath;
        segPath << outputDir << "/output_" << std::setw(3) << std::setfill('0') << (i + 1) << ".jpg";
        saveSegmentation(image, width, height, segPath.str());
        std::ostringstream buildingBlocksImagePath;
        buildingBlocksImagePath << outputDir << "/building_blocks_" << std::setw(3) << std::setfill('0') << (i + 1) << ".jpg";
        saveSegmentation(buildingBlocksImage, width, height, buildingBlocksImagePath.str());

        // Save segmentation in the folder as well
        std::ostringstream segFolderPath;
        segFolderPath << folderPath.str() << "/output.jpg";
        saveSegmentation(image, width, height, segFolderPath.str());

        // Close the JSON array
        componentInfoFile << "\n]";
        componentInfoFile.close();

        std::cout << "Component information written to components_info.json" << std::endl;
    }
}



void processImage(const std::string& imagePath, const std::string& heatmapPath,
                  const std::string& outputFolder, const Config& config) {
    // Load image data
    int width, height, channels;
    unsigned char* imgData = stbi_load(imagePath.c_str(), &width, &height, &channels, 3);
    if (!imgData) {
        std::cerr << "Failed to load image: " << imagePath << "\n";
        return;
    }
    std::cout << "Processing image: " << imagePath
              << " (Width: " << width << ", Height: " << height
              << ", Channels: " << channels << ")\n";

    // Open and read the heatmap file
    std::ifstream heatmapFile(heatmapPath, std::ios::binary);
    if (!heatmapFile) {
        std::cerr << "Failed to load heatmap file: " << heatmapPath << "\n";
        stbi_image_free(imgData);
        return;
    }
    std::vector<float> heatmap(width * height);
    heatmapFile.read(reinterpret_cast<char*>(heatmap.data()), width * height * sizeof(float));
    if (!heatmapFile) {
        std::cerr << "Error reading heatmap data from file: " << heatmapPath << "\n";
        stbi_image_free(imgData);
        return;
    }
    heatmapFile.close();

    // ---------------------------------------------------------------------
    // Compute the 80th percentile of the heatmap values (probability threshold)
    std::vector<float> sortedHeatmap = heatmap;
    std::sort(sortedHeatmap.begin(), sortedHeatmap.end());
    size_t index80_prob = static_cast<size_t>(0.8 * sortedHeatmap.size());
    if (index80_prob >= sortedHeatmap.size())
        index80_prob = sortedHeatmap.size() - 1;
    float probabilityThreshold80 = sortedHeatmap[index80_prob];
    std::cout << "Probability 80th percentile threshold: " << probabilityThreshold80 << "\n";
    // ---------------------------------------------------------------------

    // Prepare image vectors for segmentation and building blocks
    std::vector<Color> image(width * height);
    std::vector<Color> buildingBlocksImage(width * height, {255, 255, 255});
    for (int j = 0; j < width * height; ++j) {
        image[j] = {imgData[j * 3], imgData[j * 3 + 1], imgData[j * 3 + 2]};
    }
    stbi_image_free(imgData);

    // Create necessary directories
    if (!createDirectory(outputFolder)) {
        std::cerr << "Failed to create directory: " << outputFolder << "\n";
        return;
    }
    std::string buildingBlocksFolder = outputFolder + "/building_blocks";
    std::string nonBuildingBlocksFolder = outputFolder + "/non_building_blocks";
    if (!createDirectory(buildingBlocksFolder) || !createDirectory(nonBuildingBlocksFolder)) {
        std::cerr << "Failed to create subdirectories in: " << outputFolder << "\n";
        return;
    }

    // Structure to hold component details
    struct ComponentData {
        int id;
        int xMin;
        int xMax;
        int yMin;
        int yMax;
        int size;
        float avgProbability;
        std::vector<bool> mask; // Mask of the component within its bounding box
    };
    std::vector<ComponentData> components;

    // Prepare for flood fill
    std::vector<bool> visited(width * height, false);
    std::vector<bool> bigMask(width * height, false);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 255);

    auto start = std::chrono::high_resolution_clock::now();
    int componentCount = 0;

    // Loop over every pixel to extract connected components
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!visited[y * width + x]) {
                Color newColor = {static_cast<unsigned char>(distrib(gen)),
                                  static_cast<unsigned char>(distrib(gen)),
                                  static_cast<unsigned char>(distrib(gen))};

                // Reset global component metrics (assumed globals updated by floodFillIterative)
                currentComponentXmin = width;
                currentComponentXmax = 0;
                currentComponentYmin = height;
                currentComponentYmax = 0;
                currentComponentSize = 0;

                floodFillIterative(image, x, y, width, height, visited,
                                   image[y * width + x], config.k, config.use8Way,
                                   config.adj, config.euclidif, newColor, bigMask);

                int compWidth = currentComponentXmax - currentComponentXmin + 1;
                int compHeight = currentComponentYmax - currentComponentYmin + 1;
                if (currentComponentSize < config.minComponentSize ||
                    currentComponentSize < (compWidth * compHeight) / 3) {
                    for (int cx = currentComponentXmin; cx <= currentComponentXmax; cx++) {
                        for (int cy = currentComponentYmin; cy <= currentComponentYmax; cy++) {
                            bigMask[cy * width + cx] = false;
                        }
                    }
                    continue;
                }

                // Extract the component mask and compute its average heatmap probability
                float totalProbability = 0.0f;
                int pixelCount = 0;
                std::vector<bool> mask(compWidth * compHeight, false);
                for (int cy = currentComponentYmin; cy <= currentComponentYmax; cy++) {
                    for (int cx = currentComponentXmin; cx <= currentComponentXmax; cx++) {
                        if (bigMask[cy * width + cx]) {
                            int localIndex = (cy - currentComponentYmin) * compWidth + (cx - currentComponentXmin);
                            mask[localIndex] = true;
                            totalProbability += heatmap[cy * width + cx];
                            ++pixelCount;
                        }
                    }
                }
                float avgProbability = (pixelCount > 0) ? (totalProbability / pixelCount) : 0.0f;

                // Save component details
                ComponentData compData;
                compData.id = componentCount + 1;
                compData.xMin = currentComponentXmin;
                compData.xMax = currentComponentXmax;
                compData.yMin = currentComponentYmin;
                compData.yMax = currentComponentYmax;
                compData.size = currentComponentSize;
                compData.avgProbability = avgProbability;
                compData.mask = mask;
                components.push_back(compData);

                // Clear used area in bigMask
                for (int cx = currentComponentXmin; cx <= currentComponentXmax; cx++) {
                    for (int cy = currentComponentYmin; cy <= currentComponentYmax; cy++) {
                        bigMask[cy * width + cx] = false;
                    }
                }
                componentCount++;
            }
        }
    }

    // ---------------------------------------------------------------------
    // Compute the 80th percentile of component sizes (size threshold)
    std::vector<int> sizes;
    for (const auto& comp : components)
        sizes.push_back(comp.size);
    std::sort(sizes.begin(), sizes.end());
    int sizeThreshold80 = 0;
    if (!sizes.empty()) {
        int index80_size = static_cast<int>(0.9 * sizes.size());
        if (index80_size >= sizes.size())
            index80_size = sizes.size() - 1;
        sizeThreshold80 = sizes[index80_size];
    }
    std::cout << "Component size 80th percentile threshold: " << sizeThreshold80 << "\n";
    // ---------------------------------------------------------------------

    // Open the JSON file to write component info
    std::ofstream componentInfoFile(outputFolder + "/components_info.json");
    if (!componentInfoFile.is_open()) {
        std::cerr << "Failed to create components_info.json\n";
        return;
    }
    componentInfoFile << "[\n";

    // Process each component: classify and save its mask
    for (size_t i = 0; i < components.size(); ++i) {
        const auto& comp = components[i];
        int compWidth = comp.xMax - comp.xMin + 1;
        int compHeight = comp.yMax - comp.yMin + 1;
        // Use the 80th percentile probability threshold and size threshold for classification.
        bool isBuildingBlock = (comp.avgProbability >= probabilityThreshold80) && (comp.size <= sizeThreshold80);

        if (isBuildingBlock) {
            for (int cy = comp.yMin; cy <= comp.yMax; cy++) {
                for (int cx = comp.xMin; cx <= comp.xMax; cx++) {
                    int localIndex = (cy - comp.yMin) * compWidth + (cx - comp.xMin);
                    if (comp.mask[localIndex])
                        buildingBlocksImage[cy * width + cx] = {0, 0, 0};
                }
            }
        }

        std::ostringstream targetPath;
        if (isBuildingBlock)
            targetPath << buildingBlocksFolder << "/component_" << std::setw(5) << std::setfill('0')
                       << comp.id << ".jpg";
        else
            targetPath << nonBuildingBlocksFolder << "/component_" << std::setw(5) << std::setfill('0')
                       << comp.id << ".jpg";
        saveMask(comp.mask, compWidth, compHeight, targetPath.str());

        if (i > 0)
            componentInfoFile << ",\n";
        componentInfoFile << "  {\n";
        componentInfoFile << "    \"component\": " << comp.id << ",\n";
        componentInfoFile << "    \"topLeftCorner\": { \"x\": " << comp.xMin
                          << ", \"y\": " << comp.yMin << " },\n";
        componentInfoFile << "    \"width\": " << compWidth << ",\n";
        componentInfoFile << "    \"height\": " << compHeight << ",\n";
        componentInfoFile << "    \"buildingBlockProbability\": " << comp.avgProbability << "\n";
        componentInfoFile << "  }";
    }
    componentInfoFile << "\n]";
    componentInfoFile.close();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Finished processing: " << imagePath << " (Components: " << componentCount
              << ", Time: " << elapsed.count() << "s)\n";
    std::cout << "Component information written to components_info.json\n";

    // Save segmentation images
    std::ostringstream segPath;
    segPath << outputFolder << "/segmentation.jpg";
    saveSegmentation(image, width, height, segPath.str());
    std::ostringstream buildingBlocksImagePath;
    buildingBlocksImagePath << outputFolder << "/building_blocks.jpg";
    saveSegmentation(buildingBlocksImage, width, height, buildingBlocksImagePath.str());
}


int main() {
    try {
        Config config = readConfig("segmentation/config.txt");
        std::cout << "Configuration: k=" << config.k
                  << ", use8Way=" << config.use8Way
                  << ", euclidif=" << config.euclidif
                  << ", adj=" << config.adj
                  << ", minComponentSize=" << config.minComponentSize
                  << ", buildingBlockTreshold=" << config.buildingBlockTreshold<< "\n";

        processImage("preprocessing/preprocessed_data/ohcah_cpcu_000013433.jpg", "segmentation/heatmaps/data_ohcah_cpcu_000013433.hmp", "segmentation/processed_data/ohcah_cpcu_000013433/", config);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
