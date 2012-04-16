/*
    open source routing machine
    Copyright (C) Dennis Luxen, others 2010

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU AFFERO General Public License as published by
the Free Software Foundation; either version 3 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
or see http://www.gnu.org/licenses/agpl.txt.
 */

#ifndef JSON_DESCRIPTOR_H_
#define JSON_DESCRIPTOR_H_

#include <algorithm>

#include "BaseDescriptor.h"
#include "DescriptionFactory.h"
#include "../Algorithms/ObjectToBase64.h"
#include "../DataStructures/SegmentInformation.h"
#include "../DataStructures/TurnInstructions.h"
#include "../Util/Azimuth.h"
#include "../Util/StringUtil.h"

template<class SearchEngineT>
class JSONDescriptor : public BaseDescriptor<SearchEngineT>{
private:
    _DescriptorConfig config;
    DescriptionFactory descriptionFactory;
    _Coordinate current;
    unsigned numberOfEnteredRestrictedAreas;
    struct {
        int startIndex;
        int nameID;
        int leaveAtExit;
    } roundAbout;

public:
    JSONDescriptor() : numberOfEnteredRestrictedAreas(0) {}
    void SetConfig(const _DescriptorConfig & c) { config = c; }

    void Run(http::Reply & reply, RawRouteData &rawRoute, PhantomNodes &phantomNodes, SearchEngineT &sEngine, const unsigned durationOfTrip) {
        WriteHeaderToOutput(reply.content);
        if(durationOfTrip != INT_MAX) {
            descriptionFactory.SetStartSegment(phantomNodes.startPhantom);
            reply.content += "0,"
                    "\"status_message\": \"Found route between points\",";

            //Get all the coordinates for the computed route
            BOOST_FOREACH(_PathData & pathData, rawRoute.computedRouted) {
                sEngine.GetCoordinatesForNodeID(pathData.node, current);
                descriptionFactory.AppendSegment(current, pathData );
            }
            descriptionFactory.SetEndSegment(phantomNodes.targetPhantom);
        } else {
            //We do not need to do much, if there is no route ;-)
            reply.content += "207,"
                    "\"status_message\": \"Cannot find route between points\",";
        }

        descriptionFactory.Run(sEngine, config.z, durationOfTrip);
        reply.content += "\"route_geometry\": ";
        if(config.geometry) {
            descriptionFactory.AppendEncodedPolylineString(reply.content, config.encodeGeometry);
        } else {
            reply.content += "[]";
        }

        reply.content += ","
                "\"route_instructions\": [";
        if(config.instructions) {
            //Segment information has following format:
            //["instruction","streetname",length,position,time,"length","earth_direction",azimuth]
            //Example: ["Turn left","High Street",200,4,10,"200m","NE",22.5]
            //See also: http://developers.cloudmade.com/wiki/navengine/JSON_format
            unsigned prefixSumOfNecessarySegments = 0;
            roundAbout.leaveAtExit = 0;
            roundAbout.nameID = 0;
            std::string tmpDist, tmpLength, tmpDuration, tmpBearing, tmpInstruction;
            //Fetch data from Factory and generate a string from it.
            BOOST_FOREACH(const SegmentInformation & segment, descriptionFactory.pathDescription) {
                short currentInstruction = segment.turnInstruction & TurnInstructions.InverseAccessRestrictionFlag;
                numberOfEnteredRestrictedAreas += (currentInstruction != segment.turnInstruction);
                if(TurnInstructions.TurnIsNecessary( currentInstruction) ) {
                    if(TurnInstructions.EnterRoundAbout == currentInstruction) {
                        roundAbout.nameID = segment.nameID;
                        roundAbout.startIndex = prefixSumOfNecessarySegments;
                    } else {
                        if(0 != prefixSumOfNecessarySegments){
                            reply.content += ",";
                        }
                        reply.content += "[\"";
                        if(TurnInstructions.LeaveRoundAbout == currentInstruction) {
                            intToString(TurnInstructions.EnterRoundAbout, tmpInstruction);
                            reply.content += tmpInstruction;
                            reply.content += " and leave at ";
                            intToString(roundAbout.leaveAtExit+1, tmpInstruction);
                            reply.content += tmpInstruction;
                            reply.content += " exit";
                            roundAbout.leaveAtExit = 0;
                        } else {
                            intToString(currentInstruction, tmpInstruction);
                            reply.content += tmpInstruction;
                        }
                        reply.content += "\",\"";
                        reply.content += sEngine.GetEscapedNameForNameID(segment.nameID);
                        reply.content += "\",";
                        intToString(segment.length, tmpDist);
                        reply.content += tmpDist;
                        reply.content += ",";
                        intToString(prefixSumOfNecessarySegments, tmpLength);
                        reply.content += tmpLength;
                        reply.content += ",";
                        intToString(segment.duration, tmpDuration);
                        reply.content += tmpDuration;
                        reply.content += ",\"";
                        intToString(segment.length, tmpLength);
                        reply.content += tmpLength;
                        reply.content += "m\",\"";
                        reply.content += Azimuth::Get(segment.bearing);
                        reply.content += "\",";
                        doubleToStringWithTwoDigitsBehindComma(segment.bearing, tmpBearing);
                        reply.content += tmpBearing;
                        reply.content += "]";
                    }
                } else if(TurnInstructions.StayOnRoundAbout == currentInstruction) {
                    ++roundAbout.leaveAtExit;
                }
                if(segment.necessary)
                    ++prefixSumOfNecessarySegments;
            }
            if(durationOfTrip != INT_MAX) {
                reply.content += ",[\"";
                intToString(TurnInstructions.ReachedYourDestination, tmpInstruction);
                reply.content += tmpInstruction;
                reply.content += "\",\"";
                reply.content += "\",";
                reply.content += "0";
                reply.content += ",";
                intToString(prefixSumOfNecessarySegments-1, tmpLength);
                reply.content += tmpLength;
                reply.content += ",";
                reply.content += "0";
                reply.content += ",\"";
                reply.content += "\",\"";
                reply.content += Azimuth::Get(0.0);
                reply.content += "\",";
                reply.content += "0.0";
                reply.content += "]";
            }
        } else {
            BOOST_FOREACH(const SegmentInformation & segment, descriptionFactory.pathDescription) {
                short currentInstruction = segment.turnInstruction & TurnInstructions.InverseAccessRestrictionFlag;
                numberOfEnteredRestrictedAreas += (currentInstruction != segment.turnInstruction);
            }
        }
        reply.content += "],";
        //		INFO("Entered " << numberOfEnteredRestrictedAreas << " restricted areas");
        descriptionFactory.BuildRouteSummary(descriptionFactory.entireLength, durationOfTrip - ( numberOfEnteredRestrictedAreas*TurnInstructions.AccessRestrictionPenaly));

        reply.content += "\"route_summary\": {"
                "\"total_distance\":";
        reply.content += descriptionFactory.summary.lengthString;
        reply.content += ","
                "\"total_time\":";
        reply.content += descriptionFactory.summary.durationString;
        reply.content += ","
                "\"start_point\":\"";
        reply.content += sEngine.GetEscapedNameForNameID(descriptionFactory.summary.startName);
        reply.content += "\","
                "\"end_point\":\"";
        reply.content += sEngine.GetEscapedNameForNameID(descriptionFactory.summary.destName);
        reply.content += "\"";
        reply.content += "},";

        //list all viapoints so that the client may display it
        reply.content += "\"via_points\":[";
        std::string tmp;
        if(true == config.geometry) {
            for(unsigned segmentIdx = 1; segmentIdx < rawRoute.segmentEndCoordinates.size(); ++segmentIdx) {
                if(segmentIdx > 1)
                    reply.content += ",";
                reply.content += "[";
                if(rawRoute.segmentEndCoordinates[segmentIdx].startPhantom.location.isSet())
                    convertInternalReversedCoordinateToString(rawRoute.segmentEndCoordinates[segmentIdx].startPhantom.location, tmp);
                else
                    convertInternalReversedCoordinateToString(rawRoute.rawViaNodeCoordinates[segmentIdx], tmp);

                reply.content += tmp;
                reply.content += "]";
            }
        }
        reply.content += "],";
        reply.content += "\"hint_data\": {";
        reply.content += "\"checksum\":";
        intToString(rawRoute.checkSum, tmp);
        reply.content += tmp;
        reply.content += ", \"locations\": [";

        for(unsigned i = 0; i < rawRoute.segmentEndCoordinates.size(); ++i) {
            std::string hint;
            reply.content += "\"";
            EncodeObjectToBase64(rawRoute.segmentEndCoordinates[i].startPhantom, hint);
            reply.content += hint;
            reply.content += "\", ";
        }
        std::string hint;
        EncodeObjectToBase64(rawRoute.segmentEndCoordinates.back().targetPhantom, hint);
        reply.content += "\"";
        reply.content += hint;
        reply.content += "\"]";
        reply.content += "},";
        reply.content += "\"transactionId\": \"OSRM Routing Engine JSON Descriptor (v0.3)\"";
        reply.content += "}";
    }

    inline void WriteHeaderToOutput(std::string & output) {
        output += "{"
                "\"version\": 0.3,"
                "\"status\":";
    }
};
#endif /* JSON_DESCRIPTOR_H_ */
