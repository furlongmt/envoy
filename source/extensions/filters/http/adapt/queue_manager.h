

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptFilter {

class QueueManager {
 
public:
    static QueueManager& getInstance();

private:
    QueueManager();
    void run();
    
};

}
}
}
}