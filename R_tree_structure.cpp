#include <algorithm>
#include <vector>
#include <sstream>
#include <utility>
#include <limits>
#include <cfloat>
#include <cmath>
using namespace std;

#include "Record.cpp"
#include "Node.cpp"
#include "IndexfileUtilities.cpp"

#define M 50
#define m 25
#define dimension 2


struct ABLinformation{
    double minDist;
    double minMaxDist;
    int index;
    int entityNumber;
};

class Rtree{
    public:
        int rootId = 1;
        IndexfileUtilities *util;

        
        Rtree(){
            
        }
        Rtree(string datafileName){
            util = new IndexfileUtilities();

           Node *root = NULL;
           ifstream readFile;
           readFile.open(datafileName, ios::out | ios::binary);


           Record *rec;
           rec = new Record();

           short int *temp;
           for(long long int posInDisk = 0; posInDisk <= 32762 * 18 + 24026; ) {
               if(posInDisk % 32762 == 0) { // reached startOfNextBlock
                   temp = new short();
                   readFile.read((char *) temp, sizeof(short));
                   posInDisk += 2;
                   continue;
               }




               readFile.read((char *) rec, sizeof(Record));

               vector<double> coordsVector;
               coordsVector = rec->getCoords();

               Point p(coordsVector);

               int blockId = posInDisk / 32762 + 1;
               int line = (posInDisk % 32762 - 2) / (int)sizeof(Record) + 1;
               int entityNumber = getEntityNumber(blockId, line);

               insert(p, entityNumber);

               posInDisk += sizeof(Record); // 32byte
           }
           readFile.close();
        }

        int getEntityNumber(int blockId, int line) {
            int entityNumber = 0;
            entityNumber |= (blockId << 27);
            entityNumber |= line;

            return entityNumber;
        }

        void insert(Point p, int entNum){
            Node root = util->getNodeByBlockId(rootId);

            vector <Node> leafNodes(0);
            getLeafNode(root, p, leafNodes);
            for(Node node : leafNodes) {
                Rectangle rec(p, p);
                node.addChild(entNum, rec);
                node.modifiedNode();
                splitNode(node);
            }
        }

        Rectangle calculateNewBound(Point p, Rectangle rec){
            Rectangle newRectangle;

            for(int i = 0; i < dimensions; i++) {
                newRectangle.a.dim[i] = min(p.dim[i], rec.a.dim[i]);
                newRectangle.b.dim[i] = max(p.dim[i], rec.b.dim[i]);
            }

            return newRectangle;
        }

        double calculateOverlap(Rectangle a, Rectangle b){
            double overlap = 1.0;

            for(int i = 0; i < dimensions; i++) {
                double maxLeft = max(a.a.dim[i], b.a.dim[i]);
                double minRight = min(a.b.dim[i], b.b.dim[i]);

                overlap *= max(minRight - maxLeft, 0.0);
            }

            return overlap;
        }
    
        Node findChildHeuristic(Node node, Point p){
            Node childNode = util->getNodeByBlockId(node.rectangles[0].first);
            int minIndexRec = 0;
            if(childNode.isLeaf){
                vector<pair<int, Rectangle>> allRec = node.getRectangles();
                double minOverlap = DBL_MAX;
                for(int i=0;i<node.capacity;i++){
                    double sum = 0;
                    Rectangle extendedRec = calculateNewBound(p, allRec[i].second);
                    for(int j=0;j<node.capacity;j++){
                        if(i!=j){
                            sum +=  calculateOverlap(extendedRec, allRec[j].second);
                        }
                    }
                    if(sum < minOverlap){
                        minOverlap = sum;
                        minIndexRec = i;
                    }
                }

            }else{
                vector<pair<int, Rectangle>> allRec = node.getRectangles();
                double minOverlap = DBL_MAX;

                for(int i=0;i<node.capacity;i++){
                    double sum = 0;
                    Rectangle extendedRec = calculateNewBound(p, allRec[i].second);
                
                    if(calculateOverlap(extendedRec, allRec[i].second) < minOverlap){
                        minOverlap = sum;
                        minIndexRec = i;
                    }
                }
            }
            
            Node nextNode = util->getNodeByBlockId(node.rectangles[minIndexRec].first);
            return nextNode;
        }

        void getLeafNode(Node node, Point p, vector <Node> &leafNodes){
            if(node.isLeaf){
                leafNodes.push_back(node);
                return;
            }

            bool containsObj = false;
    
            for(int i=0;i<node.capacity;i++){
                if(contains(p, node.rectangles[i].second)){
                    containsObj = true;
                    int childId = node.rectangles[i].first;
                    Node currentNode = util->getNodeByBlockId(childId);
                    getLeafNode(currentNode, p, leafNodes);
                }
            }
            if(!containsObj) {
                Node chosenChild = findChildHeuristic(node, p);
                getLeafNode(chosenChild, p, leafNodes);
            }
        }


        void splitNode(Node &aNode){
            if(aNode.capacity == M+1) {
                pair<vector<pair<int, Rectangle>>, vector<pair<int ,Rectangle>>> a = splitHeuristic(aNode);
                vector<pair<int, Rectangle>> fir = a.first;
                vector<pair<int, Rectangle>> sec = a.second;

                Rectangle firBig = constructBig(fir), secBig = constructBig(sec);

                aNode.setRectangles(fir, true);
                util->updateBounds(&aNode, firBig);
                aNode.modifiedNode();
                
                int otherNodeId = util->newBlockID(secBig,aNode.isLeaf, sec, true);
                Node otherNode = util->getNodeByBlockId(otherNodeId);

                int parentId = aNode.parentId;

                if(parentId == -1){
                    Rectangle bigRec = constructBig({ { 1, firBig }, { 2, secBig } });

                    vector<pair<int, Rectangle>> emptyArr(50);
                    rootId = util->newBlockID(bigRec,false, emptyArr, false);

                    Node rootNode = util->getNodeByBlockId(rootId);
                    rootNode.addChild(aNode.blockId, aNode.boundingBox);
                    rootNode.addChild(otherNodeId, secBig);
                    rootNode.modifiedNode();

                    aNode.parentId = rootId;
                    aNode.modifiedNode();

                    otherNode.parentId = rootId;
                    otherNode.modifiedNode();

                    Node aNodeCopy = util->getNodeByBlockId(1);
                    Node otherNodeCopy = util->getNodeByBlockId(2);
                    Node rootCopy = util->getNodeByBlockId(3);
                }else{
                    Node parentNode = util->getNodeByBlockId(parentId);
                    parentNode.addChild(otherNodeId, secBig);
                    parentNode.modifiedNode();

                    otherNode.parentId = parentId;
                    otherNode.modifiedNode();

                    splitNode(parentNode);
                }
            }
        }

        Rectangle constructBig(vector <pair<int, Rectangle>> groupOfRectangles){
            Rectangle out = groupOfRectangles[0].second;
            for(int i=1;i<groupOfRectangles.size();i++){
                out = calculateNewBound(groupOfRectangles[i].second.a, out);
                out = calculateNewBound(groupOfRectangles[i].second.b, out);
            }
            return out;
        }

        double marginHeuristic(vector <pair<int, Rectangle>> &g1, vector<pair<int, Rectangle>> &g2){
            double margin1, margin2;

            margin1 = constructBig(g1).getMargin();
            margin2 = constructBig(g2).getMargin();

            return margin1 + margin2;
        }

        double areaHeuristic(vector <pair<int, Rectangle>> &g1, vector<pair<int ,Rectangle>> &g2){
            double area1, area2;

            area1 = constructBig(g1).getArea();
            area2 = constructBig(g2).getArea();

            return area1 + area2;
        }


        pair<vector<pair< int, Rectangle>>, vector<pair<int ,Rectangle>>> ChooseSplitIndex(vector<pair<int, Rectangle>> recs, int axis){
            sort(recs.begin(), recs.end(), [axis](pair<int, Rectangle> &lhs, pair<int, Rectangle> &rhs)
            {
                if(lhs.second.a.dim[axis] == rhs.second.a.dim[axis]) {
                    return lhs.second.b.dim[axis] < rhs.second.b.dim[axis];
                }

                return lhs.second.a.dim[axis] == rhs.second.a.dim[axis];
            });

            vector<pair<int, Rectangle>> minGroup1, minGroup2;
            double minCost = DBL_MAX;

            for(int k = 1; m - 1 + k < M; k++) {
                vector<pair<int, Rectangle>> group1, group2;
                int i;
                for(i=0;i<m-1+k;i++){
                    group1.push_back(recs[i]);
                }
                for(;i<recs.size();i++){
                    group2.push_back(recs[i]);
                }
                int currCost = areaHeuristic(group1, group2);

                if(currCost < minCost){
                    minCost = currCost;
                    minGroup1 = group1;
                    minGroup2 = group2;
                }
            }
            pair<vector<pair<int,Rectangle>>, vector<pair<int,Rectangle>>> out;
            out.first = minGroup1;
            out.second = minGroup2;
            return out;
        }

        int ChooseSplitAxis(vector<pair<int, Rectangle>> recs){
            int minAxis;
            double minCost = DBL_MAX;
            for(int axis = 0;axis < dimension; axis++){
                sort(recs.begin(), recs.end(), [axis](pair<int, Rectangle> &lhs, pair<int, Rectangle> &rhs)
                {
                    if(lhs.second.a.dim[axis] == rhs.second.a.dim[axis]) {
                        return lhs.second.b.dim[axis] < rhs.second.b.dim[axis];
                    }

                    return lhs.second.a.dim[axis] == rhs.second.a.dim[axis];
                });
                
                double currTotalCost = 0;

                for(int k = 1; m - 1 + k < M; k++) {
                    vector<pair<int, Rectangle>> group1, group2;
                    int i;
                    for (i = 0; i < m - 1 + k; i++) {
                        group1.push_back(recs[i]);
                    }
                    for (; i < recs.size(); i++) {
                        group2.push_back(recs[i]);
                    }
                    currTotalCost += marginHeuristic(group1, group2);
                }
                
                if(currTotalCost < minCost){
                    minCost = currTotalCost;
                    minAxis = axis;
                }
            }
            return minAxis;
        }

        pair<vector<pair<int, Rectangle>>, vector<pair<int, Rectangle>>> splitHeuristic(Node &node) {
            int axis = ChooseSplitAxis(node.getRectangles());
            return ChooseSplitIndex(node.getRectangles(), axis);
        }



        void rangeQueryUtility(int blockId, Rectangle range, vector <Record> &data){
            IndexfileUtilities util;
            Node node = util.getNodeByBlockId(blockId);

            if(node.isLeaf){
                vector<pair<int, Rectangle>> indexData = node.getRectangles();

                for(int i = 0; i < node.capacity; i++) {
                    pair<int, Rectangle> entity = indexData[i];

                    Record record = util.getRecordFromDatafile(entity.first);
                    vector <double> coordsVector = entity.second.a.getDim(); //  record.getCoords();

                    Point currPoint(coordsVector);

                    if(contains(currPoint, range)) {
                        data.push_back(record);
                    }
                }

                return;
            }

            for(int i=0;i<node.capacity;i++){
                pair<int, Rectangle> currChildBlock = node.rectangles[i];

                if(overlap(range, currChildBlock.second)){
                    rangeQueryUtility(currChildBlock.first, range, data);
                }
            }
        }
        
        vector<Record> rangeQuery(Rectangle range){
            vector<Record> answer;
            rangeQueryUtility(rootId, range, answer);
            return answer;
        }

        vector<struct ABLinformation> combine(vector<struct ABLinformation> &a, vector<struct ABLinformation> &b){
            vector<struct ABLinformation> merged;
            for(int i=0;i<a.size(); i++){
                merged.push_back(a[i]);
                merged.push_back(b[i]);
            }
            sort(merged.begin(), merged.end(), [](struct ABLinformation &lhs, struct ABLinformation &rhs)
            {
                 return lhs.minMaxDist < rhs.minMaxDist;
            });
            merged.resize(a.size());
            return merged;
        }


        vector<struct ABLinformation> knnQueryUtility(int blockId, Point point, int K){
            Node node = util->getNodeByBlockId(blockId);

            if(node.isLeaf){
                double nearestDist = DBL_MAX;
                vector<struct ABLinformation> output;

                for(int i=0;i<node.capacity;i++){
                    double minDist = MinDistance(node.rectangles[i].second, point);
                    double minMaxDist = MinMaxDistance(node.rectangles[i].second, point);
                    
                    output.push_back({ minDist, minMaxDist, i, node.rectangles[i].first });
                }

                sort(output.begin(), output.end(), [](struct ABLinformation &lhs, struct ABLinformation &rhs)
                {
                    return lhs.minMaxDist < rhs.minMaxDist;
                });
                output.resize(K);

                return output;
            }else{
                vector<struct ABLinformation> ablInfo = getABLinformation(node, point);
                vector<struct ABLinformation> ablFiltered;
                double cutoff = ablInfo[0].minMaxDist;
                copy_if (ablInfo.begin(), ablInfo.end(), back_inserter(ablFiltered),
                         [cutoff](struct ABLinformation abl){return abl.minDist <= cutoff;} );

                vector<struct ABLinformation> answer;
                for(int i=0;i<ablFiltered.size();i++){
                    int childIndex = ablFiltered[i].index;
                    int childId = node.rectangles[childIndex].first;

                    vector<struct ABLinformation> currPq = knnQueryUtility(childId, point, K);

                    answer = combine(answer, currPq);
                    int maxFromAll = answer[K-1].minDist;

                    vector<struct ABLinformation> tempAnswer;
                    copy_if (answer.begin(), answer.end(), back_inserter(tempAnswer),
                             [maxFromAll](struct ABLinformation abl){return abl.minDist <= maxFromAll;} );
                    answer = tempAnswer;
                }

                return answer;
            }
        }

        vector<struct ABLinformation> getABLinformation(Node &node, Point point){
            vector<struct ABLinformation> output;
            for(int i=0;i<node.capacity;i++){
                struct ABLinformation abl{
                    MinDistance(node.rectangles[i].second, point),
                    MinMaxDistance(node.rectangles[i].second, point),
                    i};

                output.push_back(abl);
            }

            sort(output.begin(), output.end(),[](struct ABLinformation &lhs, struct ABLinformation &rhs)
            {
                 return lhs.minMaxDist < rhs.minMaxDist;
            });

            return output;
        }

    
        
        vector<Record> knnQuery(Point point, int K){
            vector <struct ABLinformation> indexFileAns = knnQueryUtility(rootId, point,  K);
            vector <Record> eligibleRecords(0);

            for(auto currAns : indexFileAns) {
                int entityNumber = currAns.entityNumber;

                Record rec = util->getRecordFromDatafile(entityNumber);
                eligibleRecords.push_back(rec);
            }

            return eligibleRecords;
        }

        //In this function we check if two rectangles overlapping
        bool overlap(Rectangle rec1, Rectangle rec2){
            for(int i = 0; i < dimensions; i++) {
                if(max(rec1.a.dim[i], rec2.a.dim[i]) > min(rec1.b.dim[i], rec2.b.dim[i])) {
                    return false;
                }
            }

            return true;
        }


        //In this function we check if a point contains in a rectangle
        bool contains(Point point, Rectangle rec){
            for(int i = 0; i < dimensions; i++) {
                if(point.dim[i] < rec.a.dim[i] || point.dim[i] > rec.b.dim[i]) {
                    return false;
                }
            }

            return true;
        }

    private:
        double MinDistance(Rectangle rec, Point point){
            double totalDistance = 0;
            for(int i=0;i<dimensions;i++){
                int from = rec.a.dim[i];
                int to = rec.b.dim[i];
                int curr = point.dim[i];

                if(curr < from){
                    totalDistance += pow(from - curr, 2);
                }else if(curr > to){
                    totalDistance += pow(to - curr, 2);
                }
            }
            return totalDistance;
        }

        double MinMaxDistance(Rectangle rec, Point point){
            double minDistance = DBL_MAX;
            for(int i=0;i<dimensions;i++){
                double currDistance = 0;
                double from = rec.a.dim[i];
                double to = rec.b.dim[i];
                double curr = point.dim[i];

                currDistance += min(pow(from - curr, 2), pow(to - curr, 2));

                for(int j=0;j<dimensions;j++){
                    if(i!=j){
                        from = rec.a.dim[j];
                        to = rec.b.dim[j];
                        curr = point.dim[j];
                        currDistance += max(pow(from - curr, 2), pow(to - curr, 2));
                    }
                    
                }

                minDistance = min(minDistance, currDistance);
            }
            return minDistance;
        }
};