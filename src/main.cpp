
#include <io.h>
#include <fcntl.h>

#include "StandardIncludes.h"

#include "PotreeReader.h"
#include "Point.h"

#include "pmath.h"
#include "stuff.h"

//struct PointsInBox{
//	vector<Point> points;
//	int pointsProcessed = 0;
//	int nodesProcessed = 0;
//};

//struct PointsInProfile{
//	vector<Point> points;
//	int pointsProcessed = 0;
//	int nodesProcessed = 0;
//};

struct FilterResult{
	dmat4 box;
	vector<Point> points;
	int pointsProcessed = 0;
	int nodesProcessed = 0;
	int durationMS = 0;
};

///
/// The box matrix maps a unit cube to the desired oriented cube.
/// The unit cube is assumed to have a size of 1/1/1 and it is 
/// centered around the origin, i.e. coordinates are [-0.5, 0.5]
///
/// algorithm: http://www.euclideanspace.com/maths/geometry/elements/intersection/twod/index.htm
/// 
FilterResult getPointsInBox(PotreeReader *reader, dmat4 box, int minLevel, int maxLevel){
	OBB obb(box);
	
	vector<PRNode*> intersectingNodes;
	stack<PRNode*> workload({reader->root});
	
	// nodes that intersect with box
	while(!workload.empty()){
		auto node = workload.top();
		workload.pop();
	
		intersectingNodes.push_back(node);
	
		for(auto child : node->children()){
			if(child != nullptr && obb.intersects(child->boundingBox) && child->level <= maxLevel){
				workload.push(child);
			}
		}
	}

	vector<Point> accepted;

	int pointsProcessed = 0;

	for(auto node : intersectingNodes){
		if(node->level < minLevel){
			continue;
		}

		for(auto &point : node->points()){
			pointsProcessed++;

			if(obb.inside(point.position)){
				accepted.push_back(point);
			}
		}
	}

	FilterResult result;
	result.box = box;
	result.points = accepted;
	result.pointsProcessed = pointsProcessed;
	result.nodesProcessed = intersectingNodes.size();

	return result;

}

vector<FilterResult> getPointsInProfile(PotreeReader *reader, vector<dvec2> polyline, double width, int minLevel, int maxLevel){

	auto bb = reader->metadata.boundingBox;

	struct Segment{
		dvec2 start;
		dvec2 end;
		dmat4 box;
	};
	
	vector<Segment> segments;
	for(int i = 0; i < polyline.size() - 1; i++){
		dvec3 start = {polyline[i].x, polyline[i].y, bb.center().z};
		dvec3 end = {polyline[i + 1].x, polyline[i + 1].y, bb.center().z};
		dvec3 delta = end - start;
		double length = glm::length(delta);
		double angle = glm::atan(delta.y, delta.x);

		dvec3 size = {length, width, bb.size().z};

		dmat4 box = glm::translate(dmat4(), start)
			* glm::rotate(dmat4(), angle, {0.0, 0.0, 1.0}) 
			* glm::scale(dmat4(), size) 
			* glm::translate(dmat4(), {0.5, 0.0, 0.0});
		
		Segment segment = {start, end, box};
		segments.push_back(segment);
	}

	vector<FilterResult> results;

	for(Segment &segment : segments){
		auto box = segment.box;
		FilterResult result;

		Timer t = Timer("points in box").start();

		auto pointsInBox = getPointsInBox(reader, box, minLevel, maxLevel);

		t.stop();

		result.box = pointsInBox.box;
		result.points.insert(result.points.end(), pointsInBox.points.begin(), pointsInBox.points.end());
		result.pointsProcessed += pointsInBox.pointsProcessed;
		result.nodesProcessed += pointsInBox.nodesProcessed;
		result.durationMS = t.getMilli();

		results.push_back(result);
	}

	return results;
}

void savePotree(PotreeReader *reader, vector<FilterResult> results, PointAttributes pointAttributes, ostream *out){

	double scale = 0.001;

	dvec3 min = {infinity, infinity, infinity};
	dvec3 max = {-infinity, -infinity, -infinity};
	int pointsAccepted = 0;
	int pointsProcessed = 0;
	int nodesProcessed = 0;
	int durationMS = 0;

	for(auto &result : results){

		pointsAccepted += result.points.size();
		pointsProcessed += result.pointsProcessed;
		nodesProcessed += result.nodesProcessed;
		durationMS += result.durationMS;

		for(auto &p : result.points){
			min.x = std::min(min.x, p.position.x);
			min.y = std::min(min.y, p.position.y);
			min.z = std::min(min.z, p.position.z);

			max.x = std::max(max.x, p.position.x);
			max.y = std::max(max.y, p.position.y);
			max.z = std::max(max.z, p.position.z);
		}
	}

	//auto attributes = reader->metadata.pointAttributes.attributes;
	//attributes.push_back(PointAttribute::POSITION_PROJECTED_PROFILE);
	//auto pointAttributes = PointAttributes(attributes);

	{ // HEADER
		string header;
		header += "{\n";
		header += "\t\"points\": " + to_string(pointsAccepted) + ",\n";
		header += "\t\"pointsProcessed\": " + to_string(pointsProcessed) + ",\n";
		header += "\t\"nodesProcessed\": " + to_string(nodesProcessed) + ",\n";
		header += "\t\"durationMS\": " + to_string(durationMS) + ",\n";

		// BOUNDING BOX
		header += "\t\"boundingBox\": {\n";
		header += "\t\t\"lx\": " + to_string(min.x) + ",\n";
		header += "\t\t\"ly\": " + to_string(min.y) + ",\n";
		header += "\t\t\"lz\": " + to_string(min.z) + ",\n";
		header += "\t\t\"ux\": " + to_string(max.x) + ",\n";
		header += "\t\t\"uy\": " + to_string(max.y) + ",\n";
		header += "\t\t\"uz\": " + to_string(max.z) + "\n";
		header += "\t},\n";

		{ // SEGMENTS
			//header += "\t\"segments\": {\n";
			//for(auto &result : results){
			//	
			//}
			//header += "\t},\n";
		}

		header += "\t\"pointAttributes\": [\n";

		for(int i = 0; i < pointAttributes.attributes.size(); i++){
			auto attribute = pointAttributes.attributes[i];
			header += "\t\t\"" + attribute.name + "\"";

			if(i < pointAttributes.attributes.size() - 1){
				header += ",\n";
			}else{
				header += "\n";
			}
			
		}

		header += "\t],\n";

		header += "\t\"bytesPerPoint\": " + to_string(pointAttributes.byteSize) + ",\n";
		header += "\t\"scale\": " + to_string(scale) + "\n";

		header += "}\n";

		int headerSize = header.size();
		out->write(reinterpret_cast<const char*>(&headerSize), 4);
		out->write(header.c_str(), header.size());
	}
	
	double mileage = 0.0;
	for(auto &result : results){

		dmat4 box = result.box;
		OBB obb(box);
		dvec3 localMin = dvec3(box * dvec4(-0.5, -0.5, -0.5, 1.0));
		dvec3 lvx = dvec3(box * dvec4(+0.5, -0.5, -0.5, 1.0)) - localMin;
		//dvec3 lvy = dvec3(box * dvec4(-0.5, +0.5, -0.5, 1.0)) - localMin;
		//dvec3 lvz = dvec3(box * dvec4(-0.5, -0.5, +0.5, 1.0)) - localMin;

		for(Point &p : result.points){
			for(auto attribute : pointAttributes.attributes){

				if(attribute == PointAttribute::POSITION_CARTESIAN){
					unsigned int ux = (p.position.x - min.x) / scale;
					unsigned int uy = (p.position.y - min.y) / scale;
					unsigned int uz = (p.position.z - min.z) / scale;
	
					out->write(reinterpret_cast<const char *>(&ux), 4);
					out->write(reinterpret_cast<const char *>(&uy), 4);
					out->write(reinterpret_cast<const char *>(&uz), 4);
				}else if(attribute == PointAttribute::POSITION_PROJECTED_PROFILE){
					dvec3 lp = p.position - localMin;

					double dx = glm::dot(lp, obb.axes[0]) + mileage;
					//double dy = glm::dot(lp, obb.axes[1]);
					double dz = glm::dot(lp, obb.axes[2]);
					
					unsigned int ux = dx / scale;
					//unsigned int uy = dy / scale;
					unsigned int uz = dz / scale;

					out->write(reinterpret_cast<const char *>(&ux), 4);
					//cout.write(reinterpret_cast<const char *>(&uy), 4);
					out->write(reinterpret_cast<const char *>(&uz), 4);
				}else if(attribute == PointAttribute::COLOR_PACKED){
					out->write(reinterpret_cast<const char*>(&p.color), 3);
					char value = 0;
					out->write(reinterpret_cast<const char*>(&value), 1);
				}else if(attribute == PointAttribute::RGB){
					out->write(reinterpret_cast<const char*>(&p.color), 3);
				}else if(attribute == PointAttribute::INTENSITY){
					out->write(reinterpret_cast<const char*>(&p.intensity), sizeof(p.intensity));
				}else if(attribute == PointAttribute::CLASSIFICATION){
					out->write(reinterpret_cast<const char*>(&p.classification), sizeof(p.classification));
				}else{
					static long long int filler = 0;
					out->write(reinterpret_cast<const char*>(&filler), attribute.byteSize);
				}

			}

		}

		mileage += glm::length(lvx);
	}
}

void saveLAS(PotreeReader *reader, vector<FilterResult> results, PointAttributes attributes, ostream *out){
	//*out << "creating a las file";

	unsigned int numPoints = 0;
	for(auto &result : results){
		numPoints += result.points.size();
	}

	vector<char> zeroes = vector<char>(8, 0);

	out->write("LASF", 4);			// File Signature
	out->write(zeroes.data(), 2);	// File Source ID
	out->write(zeroes.data(), 2);	// Global Encoding
	out->write(zeroes.data(), 4);	// Project ID data 1
	out->write(zeroes.data(), 2);	// Project ID data 2
	out->write(zeroes.data(), 2);	// Project ID data 3
	out->write(zeroes.data(), 8);	// Project ID data 4

	// Version Major
	char versionMajor = 1;
	out->write(reinterpret_cast<const char*>(&versionMajor), 1);				

	// Version Minor
	char versionMinor = 2;
	out->write(reinterpret_cast<const char*>(&versionMinor), 1);				

	out->write("PotreeElevationProfile          ", 32);	// System Identifier
	out->write("PotreeElevationProfile          ", 32); // Generating Software

	// File Creation Day of Year
	unsigned short day = 0;
	out->write(reinterpret_cast<const char*>(&day), 2);	

	// File Creation Year
	unsigned short year = 0;
	out->write(reinterpret_cast<const char*>(&year), 2);	

	// Header Size
	unsigned short headerSize = 227;
	out->write(reinterpret_cast<const char*>(&headerSize), 2);	

	// Offset to point data
	unsigned long offsetToData = 227;
	out->write(reinterpret_cast<const char*>(&offsetToData), 4);	

	// Number variable length records
	out->write(zeroes.data(), 4);	

	// Point Data Record Format
	unsigned char pointFormat = 2;
	out->write(reinterpret_cast<const char*>(&pointFormat), 1);	

	// Point Data Record Length
	unsigned short pointRecordLength = 26;
	out->write(reinterpret_cast<const char*>(&pointRecordLength), 2);	

	// Number of points
	out->write(reinterpret_cast<const char*>(&numPoints), 4);

	// Number of points by return 0 - 4
	out->write(reinterpret_cast<const char*>(&numPoints), 4);
	out->write(zeroes.data(), 4);
	out->write(zeroes.data(), 4);
	out->write(zeroes.data(), 4);
	out->write(zeroes.data(), 4);

	// XYZ scale factors
	dvec3 scale = reader->metadata.scale;
	out->write(reinterpret_cast<const char*>(&scale), 3 * 8);

	// XYZ OFFSETS
	dvec3 offsets = reader->metadata.boundingBox.min;
	out->write(reinterpret_cast<const char*>(&offsets), 3 * 8);

	// MAX X, MIN X, MAX Y, MIN Y, MAX Z, MIN Z
	auto bb = reader->metadata.boundingBox;
	out->write(reinterpret_cast<const char*>(&bb.max.x), 8);
	out->write(reinterpret_cast<const char*>(&bb.min.x), 8);
	out->write(reinterpret_cast<const char*>(&bb.max.y), 8);
	out->write(reinterpret_cast<const char*>(&bb.min.y), 8);
	out->write(reinterpret_cast<const char*>(&bb.max.z), 8);
	out->write(reinterpret_cast<const char*>(&bb.min.z), 8);

	vector<char> buffer(pointRecordLength, 0);

	for(auto &result : results){
	
		dmat4 box = result.box;
		OBB obb(box);
		dvec3 localMin = dvec3(box * dvec4(-0.5, -0.5, -0.5, 1.0));
		dvec3 lvx = dvec3(box * dvec4(+0.5, -0.5, -0.5, 1.0)) - localMin;
	
	
	
		for(Point &p : result.points){
	
			int *ixyz = reinterpret_cast<int*>(&buffer[0]);
			unsigned short *intensity = reinterpret_cast<unsigned short*>(&buffer[12]);
			unsigned short *rgb = reinterpret_cast<unsigned short*>(&buffer[20]);
	
			ixyz[0] = (p.position.x - bb.min.x) / scale.x;
			ixyz[1] = (p.position.y - bb.min.y) / scale.y;
			ixyz[2] = (p.position.z - bb.min.z) / scale.z;
	
			intensity[0] = p.intensity;
	
			rgb[0] = p.color.r;
			rgb[1] = p.color.g;
			rgb[2] = p.color.b;
	
			out->write(buffer.data(), pointRecordLength);
		}
	}

	out->flush();







}

void save(PotreeReader *reader, vector<FilterResult> results, Arguments args){

	ostream *out = nullptr;

	if(args.hasKey("stdout")){
		out = &cout;
	}else if(args.hasKey("o")){
		string file = args.get("o", 0);

		out = new ofstream(file, std::ios::binary);
	}else{
		cerr << "ERROR: No output specified. Either -o <file> or --stdout have to be specified";
	}

	vector<PointAttribute> attributes;
	if(args.hasKey("output-attributes")){
		auto latt = args.get("output-attributes");

		for(string att : latt){
			auto attribute = PointAttribute::fromString(att);
			attributes.push_back(attribute);
		}

	}else{
		attributes = reader->metadata.pointAttributes.attributes;
		attributes.push_back(PointAttribute::POSITION_PROJECTED_PROFILE);
	}
	
	auto pointAttributes = PointAttributes(attributes);

	string outFormat = args.get("output-format", 0, "POTREE");

	if(outFormat == "POTREE"){
		savePotree(reader, results, pointAttributes, out);
	}else if(outFormat == "LAS"){
		saveLAS(reader, results, pointAttributes, out);
	}if(outFormat == "CSV"){
		//saveCSV();
		// TODO
	}

	out->flush();
	if(args.hasKey("o")){
		dynamic_cast<ofstream*>(out)->close();
		delete out;
	}
}

int main(int argc, char* argv[]){

	//cout.imbue(std::locale(""));
	//std::cout.rdbuf()->pubsetbuf( 0, 0 );

	Arguments args(argc, argv);

	if(args.hasKey("stdout")){
		_setmode( _fileno( stdout ),  _O_BINARY );
	}
	
	
	string file = args.get("", 0);
	string strPolyline = args.get("coordinates", 0);
	double width = args.getDouble("width", 0);
	int minLevel = args.getInt("min-level", 0);
	int maxLevel = args.getInt("max-level", 0);

	vector<dvec2> polyline;
	{
		strPolyline = replaceAll(strPolyline, "},{", "|");
		strPolyline = replaceAll(strPolyline, " ", "");
		strPolyline = replaceAll(strPolyline, "{", "");
		strPolyline = replaceAll(strPolyline, "}", "");

		vector<string> polylineTokens = split(strPolyline, '|');
		for(string token : polylineTokens){
			vector<string> vertexTokens = split(token, ',');

			double x = std::stod(vertexTokens[0]);
			double y = std::stod(vertexTokens[1]);

			polyline.push_back({x, y});
		}
	}
	
	PotreeReader *reader = new PotreeReader(file);

	auto results = getPointsInProfile(reader, polyline, width, minLevel, maxLevel);

	save(reader, results, args);

	cout.flush();

	return 0;
}