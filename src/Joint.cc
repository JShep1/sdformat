/*
 * Copyright 2018 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <ignition/math/Matrix4.hh>
#include <ignition/math/Pose3.hh>
#include "sdf/Error.hh"
#include "sdf/Joint.hh"
#include "sdf/JointAxis.hh"
#include "sdf/Types.hh"
#include "Utils.hh"

using namespace sdf;
using namespace ignition::math;

class sdf::JointPrivate
{
  public: JointPrivate()
  {
    // Initialize here because windows does not support list initialization
    // at member initialization (ie ... axis = {{nullptr, nullpter}};).
    this->axis[0] = nullptr;
    this->axis[1] = nullptr;
  }

  /// \brief Name of the parent link.
  public: std::string parentLinkName = "";

  /// \brief Name of the child link.
  public: std::string childLinkName = "";

  /// \brief the joint type.
  public: JointType type = JointType::INVALID;

  /// \brief Pose of the joint
  public: Pose3d pose = Pose3d::Zero;

  /// \brief Frame of the pose.
  public: std::string poseFrame = "";

  /// \brief Joint axis
  // cppcheck-suppress
  public: std::array<std::unique_ptr<JointAxis>, 2> axis;

  /// \brief Pointer to the frame graph.
  public: std::shared_ptr<FrameGraph> frameGraph = nullptr;

  /// \brief Id of the frame for this object
  public: graph::VertexId frameVertexId;

  /// \brief The SDF element pointer used during load.
  public: sdf::ElementPtr sdf;
};

/////////////////////////////////////////////////
Joint::Joint()
  : dataPtr(new JointPrivate)
{
  // Create the frame graph for the joint, and add a node for the joint.
  this->dataPtr->frameGraph.reset(new FrameGraph);
  this->dataPtr->frameVertexId = this->dataPtr->frameGraph->AddVertex(
      "", Matrix4d::Identity).Id();
}

/////////////////////////////////////////////////
Joint::Joint(Joint &&_joint)
{
  this->dataPtr = _joint.dataPtr;
  _joint.dataPtr = nullptr;
}

/////////////////////////////////////////////////
Joint::~Joint()
{
  delete this->dataPtr;
  this->dataPtr = nullptr;
}

/////////////////////////////////////////////////
Errors Joint::Load(ElementPtr _sdf,
    std::shared_ptr<FrameGraph> _frameGraph)
{
  Errors errors;

  this->dataPtr->sdf = _sdf;

  // Check that the provided SDF element is a <joint>
  // This is an error that cannot be recovered, so return an error.
  if (_sdf->GetName() != "joint")
  {
    errors.push_back({ErrorCode::ELEMENT_INCORRECT_TYPE,
        "Attempting to load a Joint, but the provided SDF element is not a "
        "<joint>."});
    return errors;
  }

  // Read the joints's name
  std::string jointName;
  if (!loadName(_sdf, jointName))
  {
    errors.push_back({ErrorCode::ATTRIBUTE_MISSING,
                     "A joint name is required, but the name is not set."});
  }

  // Read the parent link name
  std::pair<std::string, bool> parentPair =
    _sdf->Get<std::string>("parent", "");
  if (parentPair.second)
    this->dataPtr->parentLinkName = parentPair.first;
  else
  {
    errors.push_back({ErrorCode::ELEMENT_MISSING,
        "The parent element is missing."});
  }

  // Read the child link name
  std::pair<std::string, bool> childPair = _sdf->Get<std::string>("child", "");
  if (childPair.second)
    this->dataPtr->childLinkName = childPair.first;
  else
  {
    errors.push_back({ErrorCode::ELEMENT_MISSING,
        "The child element is missing."});
  }

  // Load the pose. Ignore the return value since the pose is optional.
  loadPose(_sdf, this->dataPtr->pose, this->dataPtr->poseFrame);

  // Use the parent link frame as the pose frame if the poseFrame attribute is
  // empty.
  if (this->dataPtr->poseFrame.empty())
    this->dataPtr->poseFrame = this->dataPtr->childLinkName;

  if (_sdf->HasElement("axis"))
  {
    this->dataPtr->axis[0].reset(new JointAxis());
    Errors axisErrors = this->dataPtr->axis[0]->Load(_sdf->GetElement("axis"));
    errors.insert(errors.end(), axisErrors.begin(), axisErrors.end());
  }

  if (_sdf->HasElement("axis2"))
  {
    this->dataPtr->axis[1].reset(new JointAxis());
    Errors axisErrors = this->dataPtr->axis[1]->Load(_sdf->GetElement("axis2"));
    errors.insert(errors.end(), axisErrors.begin(), axisErrors.end());
  }

  // Read the type
  std::pair<std::string, bool> typePair = _sdf->Get<std::string>("type", "");
  if (typePair.second)
  {
    std::transform(typePair.first.begin(), typePair.first.end(),
        typePair.first.begin(),
        [](unsigned char c)
        {
          return static_cast<unsigned char>(std::tolower(c));
        });
    if (typePair.first == "ball")
      this->dataPtr->type = JointType::BALL;
    else if (typePair.first == "continuous")
      this->dataPtr->type = JointType::CONTINUOUS;
    else if (typePair.first == "fixed")
      this->dataPtr->type = JointType::FIXED;
    else if (typePair.first == "gearbox")
      this->dataPtr->type = JointType::GEARBOX;
    else if (typePair.first == "prismatic")
      this->dataPtr->type = JointType::PRISMATIC;
    else if (typePair.first == "revolute")
      this->dataPtr->type = JointType::REVOLUTE;
    else if (typePair.first == "revolute2")
      this->dataPtr->type = JointType::REVOLUTE2;
    else if (typePair.first == "screw")
      this->dataPtr->type = JointType::SCREW;
    else if (typePair.first == "universal")
      this->dataPtr->type = JointType::UNIVERSAL;
    else
    {
      this->dataPtr->type = JointType::INVALID;
      errors.push_back({ErrorCode::ATTRIBUTE_INVALID,
          "Joint type of " + typePair.first +
          " is invalid. Refer to the SDF documentation for a list of "
          "valid joint types"});
    }
  }
  else
  {
    errors.push_back({ErrorCode::ATTRIBUTE_MISSING,
        "A joint type is required, but is not set."});
  }

  if (_frameGraph)
  {
    // Add a vertex in the frame graph for this joint.
    this->dataPtr->frameVertexId =
      _frameGraph->AddVertex(jointName, Matrix4d(this->dataPtr->pose)).Id();

    // Get the parent vertex based on this joints's pose frame name.
    const graph::VertexRef_M<Matrix4d>
      parentVertices = _frameGraph->Vertices(this->dataPtr->poseFrame);

    /// \todo check that parentVertices has an element, and potentially make
    /// sure it has only one element.

    // Connect the parent to the child
    _frameGraph->AddEdge({parentVertices.begin()->first,
        this->dataPtr->frameVertexId}, -1);

    // Connect the child to the parent
    _frameGraph->AddEdge({this->dataPtr->frameVertexId,
        parentVertices.begin()->first}, 1);

    this->dataPtr->frameGraph = _frameGraph;
  }
  else
  {
    errors.push_back({ErrorCode::FUNCTION_ARGUMENT_MISSING,
        "A frame graph is required to compute pose information."});
  }

  return errors;
}

/////////////////////////////////////////////////
const std::string &Joint::Name() const
{
  return this->dataPtr->frameGraph->VertexFromId(
      this->dataPtr->frameVertexId).Name();
}

/////////////////////////////////////////////////
void Joint::SetName(const std::string &_name) const
{
  // Store the name in the frame graph
  this->dataPtr->frameGraph->VertexFromId(
      this->dataPtr->frameVertexId).SetName(_name);
}

/////////////////////////////////////////////////
JointType Joint::Type() const
{
  return this->dataPtr->type;
}

/////////////////////////////////////////////////
void Joint::SetType(const JointType _jointType)
{
  this->dataPtr->type = _jointType;
}

/////////////////////////////////////////////////
const std::string &Joint::ParentLinkName() const
{
  return this->dataPtr->parentLinkName;
}

/////////////////////////////////////////////////
void Joint::SetParentLinkName(const std::string &_name) const
{
  this->dataPtr->parentLinkName = _name;
}

/////////////////////////////////////////////////
const std::string &Joint::ChildLinkName() const
{
  return this->dataPtr->childLinkName;
}

/////////////////////////////////////////////////
void Joint::SetChildLinkName(const std::string &_name) const
{
  this->dataPtr->childLinkName = _name;
}

/////////////////////////////////////////////////
const JointAxis *Joint::Axis(const unsigned int _index) const
{
  return this->dataPtr->axis[std::min(_index, 1u)].get();
}

/////////////////////////////////////////////////
Pose3d Joint::PoseInFrame(const std::string &_frame) const
{
  return poseInFrame(
      this->Name(),
      _frame.empty() ? this->PoseFrame() : _frame,
      *this->dataPtr->frameGraph);
}

/////////////////////////////////////////////////
const ignition::math::Pose3d &Joint::Pose() const
{
  return this->dataPtr->pose;
}

/////////////////////////////////////////////////
const std::string &Joint::PoseFrame() const
{
  return this->dataPtr->poseFrame;
}

/////////////////////////////////////////////////
void Joint::SetPose(const Pose3d &_pose)
{
  this->dataPtr->frameGraph->VertexFromId(
      this->dataPtr->frameVertexId).Data() = Matrix4d(_pose);
  this->dataPtr->pose = _pose;
}

/////////////////////////////////////////////////
bool Joint::SetPoseFrame(const std::string &_frame)
{
  if (_frame.empty())
    return false;

  this->dataPtr->poseFrame = _frame;
  return true;
}

/////////////////////////////////////////////////
sdf::ElementPtr Joint::Element() const
{
  return this->dataPtr->sdf;
}
