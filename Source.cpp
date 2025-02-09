#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <cassert>
#include <algorithm>
#include <future>
#include <chrono>

template <typename K, typename V>
struct Order {
    std::atomic<V>* lotSize;  // Use a pointer for atomic lotSize
    int price;

    // Default constructor
    Order() : lotSize(new std::atomic<V>(0)), price(0) {}

    // Constructor with parameters
    Order(V lotSize, int price) : lotSize(new std::atomic<V>(lotSize)), price(price) {}

    // Disable copy constructor and assignment operator
    Order(const Order& other) = delete;
    Order& operator=(const Order& other) = delete;

    // Enable move constructor and move assignment
    Order(Order&& other) noexcept : lotSize(other.lotSize), price(other.price) {
        other.lotSize = nullptr;  // Set other to a safe state
    }

    Order& operator=(Order&& other) noexcept {
        if (this != &other) {
            delete lotSize;  // Clean up existing resource
            lotSize = other.lotSize;
            price = other.price;
            other.lotSize = nullptr;  // Set other to a safe state
        }
        return *this;
    }

    // Destructor to clean up the atomic pointer
    ~Order() {
        delete lotSize;
    }
};

template <typename K, typename V>
class ConcurrentHashMap {
public:
    // Insert a new order or update an existing one
    void insert(const K& symbol, Order<K, V>&& order) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& orders = map_[symbol];
        bool found = false;

        for (auto& existingOrder : orders) {
            if (existingOrder.price == order.price) {
                existingOrder.lotSize->fetch_add(order.lotSize->load(std::memory_order_relaxed), std::memory_order_relaxed);
                found = true;
                break;
            }
        }

        if (!found) {
            orders.push_back(std::move(order));
        }
    }

    // Remove an order by symbol
    void remove(const K& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(symbol);
        if (it == map_.end()) {
            std::cerr << "Error: Symbol " << symbol << " not found for removal." << std::endl;
            return;  // Return early if symbol not found
        }
        map_.erase(it);
    }

    // Display all orders
    void display() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : map_) {
            std::cout << pair.first << ": ";
            for (const auto& order : pair.second) {
                std::cout << "{lotSize: " << order.lotSize->load() << ", price: " << order.price << "} ";
            }
            std::cout << std::endl;
        }
    }

    // Get the lowest and highest price for a given symbol
    std::pair<int, int> getPriceRange(const K& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(symbol);
        if (it == map_.end()) {
            std::cerr << "Error: Symbol " << symbol << " not found for price range." << std::endl;
            return {0, 0}; // Return {0, 0} if symbol not found
        }

        const auto& orders = it->second;
        if (orders.empty()) {
            return {0, 0};
        }

        auto minMax = std::minmax_element(orders.begin(), orders.end(),
            [](const Order<K, V>& a, const Order<K, V>& b) {
                return a.price < b.price;
            });

        return {minMax.first->price, minMax.second->price};
    }

    // Test functions for validation
    void test() {
        assert(testInsert());
        assert(testRemove());
        assert(testDisplay());
        assert(testPriceRange());
    }

private:
    std::unordered_map<K, std::vector<Order<K, V>>> map_;
    mutable std::mutex mutex_;

    // Test case for inserting orders
    bool testInsert() {
        insert("TEST", Order<K, V>(10, 2));
        {
            const auto& orders = map_.at("TEST");
            assert(orders.size() == 1);
            assert(orders[0].lotSize->load() == 10);
            assert(orders[0].price == 2);
        }
        insert("TEST", Order<K, V>(20, 2));
        {
            const auto& orders = map_.at("TEST");
            assert(orders.size() == 1);
            assert(orders[0].lotSize->load() == 30);
            assert(orders[0].price == 2);
        }
        return true;
    }

    // Test case for removing orders
    bool testRemove() {
        insert("TEST", Order<K, V>(10, 2));
        remove("TEST");
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            assert(map_.find("TEST") == map_.end());
        }
        return true;
    }

    // Test case for displaying orders
    bool testDisplay() const {
        insert("TEST", Order<K, V>(10, 2));
        display();  // This should not assert but display output
        return true;
    }

    // Test case for price range
    bool testPriceRange() const {
        insert("TEST", Order<K, V>(10, 2));
        insert("TEST", Order<K, V>(20, 5));
        insert("TEST", Order<K, V>(30, 1));
        auto range = getPriceRange("TEST");
        assert(range.first == 1);
        assert(range.second == 5);
        return true;
    }
};

int main() {
    ConcurrentHashMap<std::string, int> concurrentMap;

    // Sample symbols
    std::vector<std::string> symbols = {
        "NESTLEIND", "HDFCBANK", "RELIANCE", "TCS", "INFY",
        "SBIN", "ICICIBANK", "LT", "BAJFINANCE", "HINDUNILVR"
    };

    // Insert initial orders asynchronously
    std::vector<std::future<void>> futures;
    auto start = std::chrono::high_resolution_clock::now();
    for (const auto& symbol : symbols) {
        futures.push_back(std::async(std::launch::async, [&concurrentMap, symbol]() {
            concurrentMap.insert(symbol, Order<std::string, int>(10, 2));
        }));
    }
    for (auto& future : futures) {
        future.get();  // Ensure all insertions are completed
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Time taken for initial inserts: " << elapsed.count() << " seconds\n";

    // Test adding to existing order and adding new order asynchronously
    start = std::chrono::high_resolution_clock::now();
    auto future1 = std::async(std::launch::async, [&concurrentMap]() {
        concurrentMap.insert("NESTLEIND", Order<std::string, int>(20, 2)); 
    });
    auto future2 = std::async(std::launch::async, [&concurrentMap]() {
        concurrentMap.insert("HDFCBANK", Order<std::string, int>(15, 4));  
    });
    future1.get();
    future2.get();
    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Time taken for additional inserts: " << elapsed.count() << " seconds\n";

    // Display current orders
    start = std::chrono::high_resolution_clock::now();
    concurrentMap.display();
    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Time taken for display: " << elapsed.count() << " seconds\n";

    // Remove an order asynchronously
    start = std::chrono::high_resolution_clock::now();
    auto future3 = std::async(std::launch::async, [&concurrentMap]() {
        concurrentMap.remove("NESTLEIND");
    });
    future3.get();
    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Time taken for removal: " << elapsed.count() << " seconds\n";

    // Display after removal
    start = std::chrono::high_resolution_clock::now();
    concurrentMap.display();
    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Time taken for display after removal: " << elapsed.count() << " seconds\n";

    // Get price range asynchronously
    start = std::chrono::high_resolution_clock::now();
    auto future4 = std::async(std::launch::async, [&concurrentMap]() {
        auto range = concurrentMap.getPriceRange("HDFCBANK");
        std::cout << "Price range for HDFCBANK: {" << range.first << ", " << range.second << "}\n";
    });
    future4.get();
    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Time taken for getting price range: " << elapsed.count() << " seconds\n";

    // Run test cases
    start = std::chrono::high_resolution_clock::now();
    concurrentMap.test();
    end = std::chrono::high_resolution_clock::now();
    elapsed = end - start;
    std::cout << "Time taken for tests: " << elapsed.count() << " seconds\n";

    return 0;
}
