/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// SkeletalRepresentationInitializer Logic includes
#include "vtkSlicerSkeletalRepresentationInitializerLogic.h"

// MRML includes
#include <vtkMRMLScene.h>
#include <vtkMRMLModelNode.h>
#include <vtkMRMLModelDisplayNode.h>
#include <vtkMRMLDisplayNode.h>
#include <vtkMRMLMarkupsDisplayNode.h>
#include <vtkMRMLMarkupsFiducialNode.h>
#include <vtkMRMLMarkupsNode.h>
#include "vtkSlicerMarkupsLogic.h"

// VTK includes
#include <vtkIntArray.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkCenterOfMass.h>
#include <vtkObjectFactory.h>
#include <vtkCurvatures.h>
#include <vtkWindowedSincPolyDataFilter.h>
#include <vtkSmoothPolyDataFilter.h>
#include <vtkParametricEllipsoid.h>
#include <vtkParametricFunctionSource.h>
#include <vtkImplicitPolyDataDistance.h>
#include <vtkPolyDataNormals.h>

#include <vtkPoints.h>
#include <vtkLine.h>
#include <vtkQuad.h>
#include <vtkPolyData.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkPolyDataReader.h>
#include <vtkPolyDataWriter.h>
#include <vtkMassProperties.h>

// Eigen includes
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

// STD includes
#include <cassert>
#include <iostream>

// vtk system tools
#include <vtksys/SystemTools.hxx>

#include "vtkBackwardFlowLogic.h"
#include "qSlicerApplication.h"
#include <QString>

#define MAX_FILE_NAME  256
//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerSkeletalRepresentationInitializerLogic);

//----------------------------------------------------------------------------
vtkSlicerSkeletalRepresentationInitializerLogic::vtkSlicerSkeletalRepresentationInitializerLogic()
{
}

//----------------------------------------------------------------------------
vtkSlicerSkeletalRepresentationInitializerLogic::~vtkSlicerSkeletalRepresentationInitializerLogic()
{
}

//----------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationInitializerLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationInitializerLogic::SetMRMLSceneInternal(vtkMRMLScene * newScene)
{
  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::NodeAddedEvent);
  events->InsertNextValue(vtkMRMLScene::NodeRemovedEvent);
  events->InsertNextValue(vtkMRMLScene::EndBatchProcessEvent);
  this->SetAndObserveMRMLSceneEventsInternal(newScene, events.GetPointer());
}

//-----------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationInitializerLogic::RegisterNodes()
{
  assert(this->GetMRMLScene() != 0);
}

//---------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationInitializerLogic::UpdateFromMRMLScene()
{
  assert(this->GetMRMLScene() != 0);
}

//---------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationInitializerLogic
::OnMRMLSceneNodeAdded(vtkMRMLNode* vtkNotUsed(node))
{
}

//---------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationInitializerLogic
::OnMRMLSceneNodeRemoved(vtkMRMLNode* vtkNotUsed(node))
{
}

// flow surface in one step
// basic idea: When the user select a mesh file, make a copy of vtk file in the application path.
// In each step of flow, read in that copy, flow it and save it the same place with same name.
// TODO: cleanup the hard disk when the module exits
int vtkSlicerSkeletalRepresentationInitializerLogic::FlowSurfaceOneStep(double dt, double smooth_amount)
{
    std::cout << "flow one step : dt-" << dt << std::endl;
    std::cout << "flow one step : smooth amount-" << smooth_amount << std::endl;

    char tempFileName[MAX_FILE_NAME];
    sprintf(tempFileName, "%s/temp_output.vtk", this->GetApplicationLogic()->GetTemporaryPath());

    vtkSmartPointer<vtkPolyDataReader> reader = vtkSmartPointer<vtkPolyDataReader>::New();
    reader->SetFileName(tempFileName);
    reader->Update();
    vtkSmartPointer<vtkPolyData> mesh = reader->GetOutput();
    if(mesh == NULL)
    {
        vtkErrorMacro("No mesh has read in this module. Please select input mesh file first.");
        return -1;
    }
    vtkSmartPointer<vtkMassProperties> mass_filter =
        vtkSmartPointer<vtkMassProperties>::New();
    mass_filter->SetInputData(mesh);
    mass_filter->Update();
    double original_volume = mass_filter->GetVolume();
//    std::cout << "Original Volume: " << original_volume << std::endl;
    vtkSmartPointer<vtkWindowedSincPolyDataFilter> smooth_filter =
        vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
    smooth_filter->SetPassBand(smooth_amount);
    smooth_filter->NonManifoldSmoothingOn();
    smooth_filter->NormalizeCoordinatesOn();
    smooth_filter->SetNumberOfIterations(20);
    smooth_filter->FeatureEdgeSmoothingOff();
    smooth_filter->BoundarySmoothingOff();
    smooth_filter->SetInputData(mesh);
    smooth_filter->Update();
    if(smooth_amount > 0) {
        mesh = smooth_filter->GetOutput();
    }

//    normal filter
    vtkSmartPointer<vtkPolyDataNormals> normal_filter =
        vtkSmartPointer<vtkPolyDataNormals>::New();
    normal_filter->SplittingOff();
    normal_filter->ComputeCellNormalsOff();
    normal_filter->ComputePointNormalsOn();
    normal_filter->SetInputData(mesh);
    normal_filter->Update();
    // curvature filter
    vtkSmartPointer<vtkCurvatures> curvature_filter =
        vtkSmartPointer<vtkCurvatures>::New();
    curvature_filter->SetCurvatureTypeToMean();
    curvature_filter->SetInputData(mesh);
    curvature_filter->Update();

    vtkSmartPointer<vtkDoubleArray> H =
        vtkDoubleArray::SafeDownCast(curvature_filter->GetOutput()->GetPointData()->GetArray("Mean_Curvature"));
    if(H == NULL) {
        vtkErrorMacro("error in getting mean curvature");
        return -1;
    }
    vtkDataArray* N = normal_filter->GetOutput()->GetPointData()->GetNormals();
    if(N == NULL) {
        vtkErrorMacro("error in getting normals");
        return -1;
    }

    // perform the flow
    vtkSmartPointer<vtkPoints> points = mesh->GetPoints();

    for(int i = 0; i < points->GetNumberOfPoints(); ++i) {
        double p[3];
        points->GetPoint(i, p);
        double curr_N[3];
        N->GetTuple(i, curr_N);
        double curr_H = H->GetValue(i);

        for(int idx = 0; idx < 3; ++idx) {
            p[idx] -= dt * curr_H * curr_N[idx];
        }
        points->SetPoint(i, p);
    }
    points->Modified();

    mass_filter->SetInputData(mesh);
    mass_filter->Update();
    double curr_volume = mass_filter->GetVolume();
    for(int i = 0; i < points->GetNumberOfPoints(); ++i) {
        double p[3];
        points->GetPoint(i, p);
        for(int j = 0; j < 3; ++j) {
            p[j] *= std::pow( original_volume / curr_volume , 1.0 / 3.0 );
        }
//        points->SetPoint(i, p);
    }
    points->Modified();

    // firstly get other intermediate result invisible
    HideNodesByNameByClass("curvature_flow_result","vtkMRMLModelNode");
    HideNodesByNameByClass("best_fitting_ellipsoid_polydata", "vtkMRMLModelNode");

    // then add this new intermediate result
    std::string modelName("curvature_flow_result");
    AddModelNodeToScene(mesh, modelName.c_str(), true);

    vtkSmartPointer<vtkPolyDataWriter> writer =
        vtkSmartPointer<vtkPolyDataWriter>::New();
    writer->SetInputData(mesh);
    writer->SetFileName(tempFileName);
    writer->Update();

    // compute the fitting ellipsoid
    vtkSmartPointer<vtkCenterOfMass> centerMassFilter =
        vtkSmartPointer<vtkCenterOfMass>::New();
    centerMassFilter->SetInputData(mesh);
    centerMassFilter->SetUseScalarsAsWeights(false);
    centerMassFilter->Update();
    double center[3];
    centerMassFilter->GetCenter(center);

//    ShowFittingEllipsoid(points, curr_volume, center);
    return 0;
}
int vtkSlicerSkeletalRepresentationInitializerLogic::SetInputFileName(const std::string &filename)
{
    vtkSmartPointer<vtkPolyDataReader> reader =
        vtkSmartPointer<vtkPolyDataReader>::New();
    reader->SetFileName(filename.c_str());
    reader->Update();

    vtkSmartPointer<vtkPolyData> mesh;
    mesh = reader->GetOutput();
    // output the original mesh
    std::string modelName("original");
    AddModelNodeToScene(mesh, modelName.c_str(), true, 0.88, 0.88, 0.88);

    // save
    std::string tempDir = this->GetApplicationLogic()->GetTemporaryPath();
    char tempFileName[MAX_FILE_NAME];
    sprintf(tempFileName, "%s/temp_output.vtk", tempDir.c_str());

    vtkSmartPointer<vtkPolyDataWriter> writer =
        vtkSmartPointer<vtkPolyDataWriter>::New();
    writer->SetInputData(mesh);
    writer->SetFileName(tempFileName);
    writer->Update();

    // TODO: delete this part if genuine backflow works
    std::string directory;
    const size_t last_slash_idx = filename.rfind('/');
    if (std::string::npos == last_slash_idx)
    {
        return -1;
    }
    directory = filename.substr(0, last_slash_idx);

    std::string ellFile("/best_fitting_ellipsoid.vtk");
    std::string modelFile("/srep.m3d");
    std::string ellModel("/ellipsoid.m3d");
    vtksys::SystemTools::CopyAFile(directory + ellFile, tempDir + ellFile, true);
    vtksys::SystemTools::CopyAFile(directory + modelFile, tempDir + modelFile, true);
    vtksys::SystemTools::CopyAFile(directory + ellModel, tempDir + ellModel, true);
    ///////////////////////////////////////
    return 0;
}

// flow surface to the end: either it's ellipsoidal enough or reach max_iter
int vtkSlicerSkeletalRepresentationInitializerLogic::FlowSurfaceMesh(const std::string &filename, double dt, double smooth_amount, int max_iter, int freq_output)
{
    // std::cout << filename << std::endl;
    // std::cout << dt << std::endl;
    // std::cout << smooth_amount << std::endl;
    // std::cout << max_iter << std::endl;
    // std::cout << freq_output << std::endl;
    vtkSmartPointer<vtkPolyDataReader> reader =
        vtkSmartPointer<vtkPolyDataReader>::New();
    reader->SetFileName(filename.c_str());
    reader->Update();


    vtkSmartPointer<vtkPolyData> mesh =
        vtkSmartPointer<vtkPolyData>::New();
    mesh = reader->GetOutput();

    vtkSmartPointer<vtkMassProperties> mass_filter =
        vtkSmartPointer<vtkMassProperties>::New();
    mass_filter->SetInputData(mesh);
    mass_filter->Update();
    double original_volume = mass_filter->GetVolume();
//    std::cout << "Original Volume: " << original_volume << std::endl;

    // default parameters
    // double dt = 0.001;
    // double smooth_amount = 0.03;
    // int max_iter = 300;
    int iter = 0;
    double tolerance = 0.05;
    double q = 1.0;

    // create folder if not exist
    char forwardFolder[MAX_FILE_NAME];
    const char *tempFolder = this->GetApplicationLogic()->GetTemporaryPath();
    sprintf(forwardFolder, "%s/forward", tempFolder);
    std::cout << "forward folder" << forwardFolder << std::endl;
    if (!vtksys::SystemTools::FileExists(forwardFolder, false))
    {
      if (!vtksys::SystemTools::MakeDirectory(forwardFolder))
      {
              std::cout << "Failed to create folder : " << forwardFolder << std::endl;

      }
    }
    while(q > tolerance && iter < max_iter) {
        // smooth filter
        vtkSmartPointer<vtkWindowedSincPolyDataFilter> smooth_filter =
            vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
        smooth_filter->SetPassBand(smooth_amount);
        smooth_filter->NonManifoldSmoothingOn();
        smooth_filter->NormalizeCoordinatesOn();
        smooth_filter->SetNumberOfIterations(20);
        smooth_filter->FeatureEdgeSmoothingOff();
        smooth_filter->BoundarySmoothingOff();
        smooth_filter->SetInputData(mesh);
        smooth_filter->Update();
        if(smooth_amount > 0) {
            mesh = smooth_filter->GetOutput();
        }

        // normal filter
        vtkSmartPointer<vtkPolyDataNormals> normal_filter =
            vtkSmartPointer<vtkPolyDataNormals>::New();
        normal_filter->SplittingOff();
        normal_filter->ComputeCellNormalsOff();
        normal_filter->ComputePointNormalsOn();
        normal_filter->SetInputData(mesh);
        normal_filter->Update();
        // curvature filter
        vtkSmartPointer<vtkCurvatures> curvature_filter =
            vtkSmartPointer<vtkCurvatures>::New();
        curvature_filter->SetCurvatureTypeToMean();
        curvature_filter->SetInputData(mesh);
        curvature_filter->Update();


        vtkSmartPointer<vtkDoubleArray> H =
            vtkDoubleArray::SafeDownCast(curvature_filter->GetOutput()->GetPointData()->GetArray("Mean_Curvature"));
        if(H == NULL) {
            std::cerr << "error in getting mean curvature" << std::endl;
            return EXIT_FAILURE;
        }
        vtkDataArray* N = normal_filter->GetOutput()->GetPointData()->GetNormals();
        if(N == NULL) {
            std::cerr << "error in getting normals" << std::endl;
            return EXIT_FAILURE;
        }

        // perform the flow
        vtkSmartPointer<vtkPoints> points = mesh->GetPoints();
        for(int i = 0; i < points->GetNumberOfPoints(); ++i) {
            double p[3];
            points->GetPoint(i, p);
            double curr_N[3];
            N->GetTuple(i, curr_N);
            double curr_H = H->GetValue(i);
            for(int idx = 0; idx < 3; ++idx) {
                p[idx] -= dt * curr_H * curr_N[idx];
            }
            points->SetPoint(i, p);
        }
        points->Modified();
        // double test_point[3];
        // points->GetPoint(10, test_point);
        // std::cout << test_point[0] << " , " << test_point[1] << " , " << test_point[2] << std::endl;

        mass_filter->SetInputData(mesh);
        mass_filter->Update();
        double curr_volume = mass_filter->GetVolume();
        for(int i = 0; i < points->GetNumberOfPoints(); ++i) {
            double p[3];
            points->GetPoint(i, p);
            for(int j = 0; j < 3; ++j) {
                p[j] *= std::pow( original_volume / curr_volume , 1.0 / 3.0 );
            }
//            points->SetPoint(i, p);
        }
        points->Modified();
        // TODO: move to the proper directory
        // save the result for the purpose of backward flow
        char fileName[MAX_FILE_NAME];
        sprintf(fileName, "%s/forward_output#%04d.vtk", forwardFolder, iter+1);

        vtkSmartPointer<vtkPolyDataWriter> writer =
            vtkSmartPointer<vtkPolyDataWriter>::New();
        writer->SetInputData(mesh);
        writer->SetFileName(fileName);
        writer->Update();

        if((iter +1) % freq_output == 0)
        {
            char modelName[128];
            sprintf(modelName, "output#%04d", iter+1);
            AddModelNodeToScene(mesh, modelName, false);
            vtkSmartPointer<vtkCenterOfMass> centerMassFilter =
                vtkSmartPointer<vtkCenterOfMass>::New();
            centerMassFilter->SetInputData(mesh);
            centerMassFilter->SetUseScalarsAsWeights(false);
            centerMassFilter->Update();
            double center[3];
            centerMassFilter->GetCenter(center);
        }
        q -= 0.0001;
        iter++;
    }
    forwardCount = iter;
    double rx, ry, rz;
    ShowFittingEllipsoid(mesh, rx, ry, rz);


    GenerateSrepForEllipsoid(mesh, 5, 5);
    return 1;
}

void vtkSlicerSkeletalRepresentationInitializerLogic::AddModelNodeToScene(vtkPolyData* mesh, const char* modelName, bool isModelVisible, double r, double g, double b)
{
    std::cout << "AddModelNodeToScene: parameters:" << modelName << std::endl;
    vtkMRMLScene *scene = this->GetMRMLScene();
    if(!scene)
    {
        vtkErrorMacro(" Invalid scene");
        return;
    }

    // model node
    vtkSmartPointer<vtkMRMLModelNode> modelNode;
    modelNode = vtkSmartPointer<vtkMRMLModelNode>::New();
    modelNode->SetScene(scene);

    modelNode->SetName(modelName);
    modelNode->SetAndObservePolyData(mesh);

    // display node
    vtkSmartPointer<vtkMRMLModelDisplayNode> displayModelNode;

    displayModelNode = vtkSmartPointer<vtkMRMLModelDisplayNode>::New();
    if(displayModelNode == NULL)
    {
        vtkErrorMacro("displayModelNode is NULL");
        return;
    }
    displayModelNode->SetColor(r, g, b);
    displayModelNode->SetScene(scene);
    displayModelNode->SetLineWidth(2.0);
    displayModelNode->SetBackfaceCulling(0);
    displayModelNode->SetRepresentation(1);
    if(isModelVisible)
    {
        // make the 1st mesh after flow visible
        displayModelNode->SetVisibility(1);
    }
    else
    {
        displayModelNode->SetVisibility(0);
    }

    scene->AddNode(displayModelNode);
    modelNode->AddAndObserveDisplayNodeID(displayModelNode->GetID());

    scene->AddNode(modelNode);

}
int vtkSlicerSkeletalRepresentationInitializerLogic::ShowFittingEllipsoid(vtkPolyData* mesh, double &rx, double &ry, double &rz)
{
    vtkSmartPointer<vtkPoints> points = mesh->GetPoints();
    Eigen::MatrixXd point_matrix(points->GetNumberOfPoints(), 3);
    for(int i = 0; i < points->GetNumberOfPoints(); ++i)
    {
        double p[3];
        points->GetPoint(i, p);
        point_matrix.row(i) << p[0], p[1], p[2];
    }
    // compute best fitting ellipsoid
    // For now assume that the surface is centered and rotationally aligned
    // 1. compute the second moment after centering the data points
    Eigen::MatrixXd center = point_matrix.colwise().mean();
    Eigen::MatrixXd centered_point_mat = point_matrix - center.replicate(point_matrix.rows(), 1);
    Eigen::MatrixXd point_matrix_transposed = centered_point_mat.transpose();
    Eigen::Matrix3d second_moment = point_matrix_transposed * centered_point_mat;
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(second_moment);
    Eigen::VectorXd radii = es.eigenvalues();
    radii(0) = sqrt(radii(0));
    radii(1) = sqrt(radii(1));
    radii(2) = sqrt(radii(2));

    double ellipsoid_volume = 4 / 3.0 * vtkMath::Pi() * radii(0) * radii(1) * radii(2);
    vtkSmartPointer<vtkMassProperties> mass =
        vtkSmartPointer<vtkMassProperties>::New();
    mass->SetInputData(mesh);
    mass->Update();

    double volume_factor = pow(mass->GetVolume() / ellipsoid_volume, 1.0 / 3.0);
    radii(0) *= volume_factor;
    radii(1) *= volume_factor;
    radii(2) *= volume_factor;
    // obtain the best fitting ellipsoid from the second moment matrix
    vtkSmartPointer<vtkParametricEllipsoid> ellipsoid =
        vtkSmartPointer<vtkParametricEllipsoid>::New();
    ellipsoid->SetXRadius(radii(0));
    ellipsoid->SetYRadius(radii(1));
    ellipsoid->SetZRadius(radii(2));

    vtkSmartPointer<vtkParametricFunctionSource> parametric_function =
        vtkSmartPointer<vtkParametricFunctionSource>::New();
    parametric_function->SetParametricFunction(ellipsoid);
    parametric_function->SetUResolution(30);
    parametric_function->SetVResolution(30);
    parametric_function->Update();
    vtkSmartPointer<vtkPolyData> ellipsoid_polydata = parametric_function->GetOutput();

    using namespace Eigen;
    // Get ellipsoid points into the matrix
    MatrixXd ellipsoid_points_matrix(ellipsoid_polydata->GetNumberOfPoints(), 3);
    for(int i = 0; i < ellipsoid_polydata->GetNumberOfPoints(); ++i) {
        double p[3];
        ellipsoid_polydata->GetPoint(i,p);
        ellipsoid_points_matrix(i,0) = p[0];
        ellipsoid_points_matrix(i,1) = p[1];
        ellipsoid_points_matrix(i,2) = p[2];
    }
    MatrixXd rotation;
    rotation = es.eigenvectors(); // 3 by 3 rotation matrix

    // rotate the points
    MatrixXd rotated_ellipsoid_points = rotation * (ellipsoid_points_matrix.transpose());
    rotated_ellipsoid_points.transposeInPlace(); // n x 3
    // translate the points
    MatrixXd translated_points = rotated_ellipsoid_points + center.replicate(rotated_ellipsoid_points.rows(),1);

    // convert eigen matrix to vtk polydata
    vtkSmartPointer<vtkPolyData> best_fitting_ellipsoid_polydata =
        vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints> best_fitting_ellipsoid_points =
        vtkSmartPointer<vtkPoints>::New();
    for(int i = 0; i < translated_points.rows(); ++i) {
        double p[3] = {translated_points(i,0), translated_points(i,1), translated_points(i,2)};
        best_fitting_ellipsoid_points->InsertNextPoint(p);
    }
    best_fitting_ellipsoid_polydata->SetPoints(best_fitting_ellipsoid_points);
    best_fitting_ellipsoid_polydata->SetPolys(ellipsoid_polydata->GetPolys());
    best_fitting_ellipsoid_polydata->Modified();

    AddModelNodeToScene(best_fitting_ellipsoid_polydata, "best_fitting_ellipsoid", true, 1, 1, 0);
//    vtkSmartPointer<vtkPolyDataWriter> writer =
//        vtkSmartPointer<vtkPolyDataWriter>::New();
//    writer->SetInputData(best_fitting_ellipsoid_polydata);
//    writer->SetFileName("ellipsoid.vtk");
//    writer->Update();
    rx = radii(2); ry = radii(1); rz = radii(0);
    return 0;
}

const double ELLIPSE_SCALE = 0.9;
const double EPS = 0.0001;
int vtkSlicerSkeletalRepresentationInitializerLogic::GenerateSrepForEllipsoid(vtkPolyData *mesh, int nRows, int nCols)
{
    using namespace Eigen;
    // the number of rows should be odd number
    double shift = 0.02; // shift fold curve off the inner spokes
    nRows = 5; nCols = 5; // TODO: accept input values from user interface

    // 1. derive the best fitting ellipsoid from the deformed mesh
    vtkSmartPointer<vtkPoints> points = mesh->GetPoints();
    MatrixXd point_matrix(points->GetNumberOfPoints(), 3);
    for(int i = 0; i < points->GetNumberOfPoints(); ++i)
    {
        double p[3];
        points->GetPoint(i, p);
        point_matrix.row(i) << p[0], p[1], p[2];
    }
    MatrixXd center = point_matrix.colwise().mean();
    MatrixXd centered_point_mat = point_matrix - center.replicate(point_matrix.rows(), 1);
    MatrixXd point_matrix_transposed = centered_point_mat.transpose();
    Matrix3d second_moment = point_matrix_transposed * centered_point_mat;
    SelfAdjointEigenSolver<Eigen::MatrixXd> es_obj(second_moment);
    VectorXd radii = es_obj.eigenvalues();

    double rz = sqrt(radii(0));
    double ry = sqrt(radii(1));
    double rx = sqrt(radii(2));
    double ellipsoid_volume = 4 / 3.0 * vtkMath::Pi() * rx * ry * rz;
    vtkSmartPointer<vtkMassProperties> mass =
        vtkSmartPointer<vtkMassProperties>::New();
    mass->SetInputData(mesh);
    mass->Update();

    double volume_factor = pow(mass->GetVolume() / ellipsoid_volume, 1.0 / 3.0);
    rz *= volume_factor;
    ry *= volume_factor;
    rx *= volume_factor;

    double mrx_o = (rx*rx-rz*rz)/rx;
    double mry_o = (ry*ry-rz*rz)/ry;
    double mrb = mry_o * ELLIPSE_SCALE;
    double mra = mrx_o * ELLIPSE_SCALE;

    // 2. compute the skeletal points
    int nCrestPoints = nRows*2 + (nCols-2)*2;
    double deltaTheta = 2*vtkMath::Pi()/nCrestPoints;
    MatrixXd skeletal_points_x(nRows, nCols);
    MatrixXd skeletal_points_y(nRows, nCols);
    //MatrixXd skeletal_points_z(nRows, nCols);
    int r = 0, c = 0;
    for(int i = 0; i < nCrestPoints; ++i)
    {
        double theta = vtkMath::Pi() - deltaTheta * floor(nRows/2) - deltaTheta*i;
        double x = mra * cos(theta);
        double y = mrb * sin(theta);

        // these crest points have no inward points (side or corner of the s-rep)
        skeletal_points_x(r, c) = x;
        skeletal_points_y(r, c) = y;
        //skeletal_points_z(r, c) = z;
        if(i < nCols - 1)
        {
            // top row of crest points
            c += 1;
        }
        else if(i < nCols - 1 + nRows - 1)
        {
            // right side col of crest points ( if the top-left point is the origin)
            r = r + 1;
        }
        else if(i < nCols - 1 + nRows - 1 + nCols - 1)
        {
            // bottom row of crest points
            c = c - 1;
        }
        else
        {
            // left side col of crest points
            r = r - 1;
        }
        if((i < nCols - 1 && i > 0) || (i > nCols + nRows - 2 && i < 2*nCols + nRows - 3))
        {
            // compute skeletal points inward
            double mx_ = (mra * mra - mrb * mrb) * cos(theta) / mra; // this is the middle line
            double my_ = .0;
            double dx_ = x - mx_;
            double dy_ = y - my_;
            int numSteps = floor(nRows/2); // steps from crest point to the skeletal point
            double stepSize = 1.0 / double(numSteps); // step size on the half side of srep
            for(int j = 0; j <= numSteps; ++j)
            {
                double tempX_ = mx_ + stepSize * j * dx_;
                double tempY_ = my_ + stepSize * j * dy_;
                if(i < nCols - 1)
                {
                    // step from medial to top at current iteration on the top line
                    int currR = numSteps - j;
                    skeletal_points_x(currR, c-1) = tempX_;
                    skeletal_points_y(currR, c-1) = tempY_;
                }
                else
                {
                    int currR = j + numSteps;
                    skeletal_points_x(currR, c+1) = tempX_;
                    skeletal_points_y(currR, c+1) = tempY_;
                }

            }

        }
    }

    // 3. compute the head points of spokes
    MatrixXd skeletal_points(nRows*nCols, 3);
    MatrixXd bdry_points_up(nRows*nCols, 3);
    MatrixXd bdry_points_down(nRows*nCols, 3);
    MatrixXd bdry_points_crest(nCrestPoints, 3);
    MatrixXd skeletal_points_crest(nCrestPoints, 3);
    int id_pt = 0; int id_crest = 0;
    MatrixXd shift_dir(nCrestPoints, 3); // shift direction for every crest point
    for(int i = 0; i < nRows; ++i)
    {
        for(int j = 0; j < nCols; ++j)
        {
            double mx = skeletal_points_x(i,j);
            double my = skeletal_points_y(i,j);
            double sB = my * mrx_o;
            double cB = mx * mry_o;
            double l = sqrt(sB*sB + cB*cB);
            double sB_n, cB_n;
            if(l == 0)
            {
                sB_n = sB;
                cB_n = cB;
            }
            else
            {
                sB_n = sB / l;
                cB_n = cB / l;
            }
            double cA = l / (mrx_o * mry_o);
            double sA = sqrt(1 - cA*cA);
            double sx = rx * cA * cB_n - mx;
            double sy = ry * cA * sB_n - my;
            double sz = rz * sA;

            double bx = (sx + mx);
            double by = (sy + my);
            double bz = (sz);

            skeletal_points.row(id_pt) << mx, my, 0.0;
            bdry_points_up.row(id_pt) << bx, by, bz;
            bdry_points_down.row(id_pt) << bx, by, -bz;
            id_pt++;
            // fold curve
            if(i == 0 || i == nRows - 1 || j == 0 || j == nCols - 1)
            {
                double cx = rx * cB_n - mx;
                double cy = ry * sB_n - my;
                double cz = 0;
                Vector3d v, v2, v3;
                v << cx, cy, cz;
                v2 << sx, sy, 0.0;
                double v_n = v.norm();
                v2.normalize(); // v2 is the unit vector pointing out to norm dir
                v3 = v_n * v2;
                double bx = (v3(0) + mx);
                double by = (v3(1) + my);
                double bz = v3(2);
                bdry_points_crest.row(id_crest) << bx, by, bz;
                skeletal_points_crest.row(id_crest) << mx, my, 0.0;
                shift_dir.row(id_crest) << v2(0), v2(1), v2(2);
                id_crest++;
            }
        }
    }

    // 4. transform the s-rep
    MatrixXd transpose_srep = skeletal_points.transpose(); // 3xn
    Matrix3d srep_secondMoment = transpose_srep * skeletal_points; // 3x3
    SelfAdjointEigenSolver<Eigen::MatrixXd> es_srep(srep_secondMoment);

    Matrix3d rotation;
    rotation = es_obj.eigenvectors(); // 3 by 3 rotation relative to deformed object
    Matrix3d rot_srep;
    rot_srep = es_srep.eigenvectors().transpose();
    rotation = rotation * rot_srep;

    // all skeletal points
    MatrixXd trans_srep = (rotation * transpose_srep).transpose();
    MatrixXd transformed_skeletal_points = trans_srep+
            center.replicate(trans_srep.rows(), 1);

    // up spoke head point on the bdry
    MatrixXd transpose_up_pdm = bdry_points_up.transpose();
    MatrixXd trans_up_pdm = (rotation * transpose_up_pdm).transpose();
    MatrixXd transformed_up_pdm =  trans_up_pdm +
            center.replicate(trans_up_pdm.rows(), 1);

    // down spoke head point on the bdry
    MatrixXd transpose_down_pdm = bdry_points_down.transpose();
    MatrixXd trans_down_pdm = (rotation * transpose_down_pdm).transpose();
    MatrixXd transformed_down_pdm = trans_down_pdm +
            center.replicate(trans_down_pdm.rows(), 1);

    // crest head point on the bdry
    MatrixXd transpose_crest_pdm = bdry_points_crest.transpose();
    MatrixXd trans_crest_pdm = (rotation * transpose_crest_pdm).transpose();
    MatrixXd transformed_crest_pdm = trans_crest_pdm + center.replicate(trans_crest_pdm.rows(), 1);

    // crest base point on the skeletal sheet
    MatrixXd transpose_crest_base = skeletal_points_crest.transpose();
    MatrixXd trans_crest_base = (rotation * transpose_crest_base).transpose();
    MatrixXd transformed_crest_base = trans_crest_base + center.replicate(trans_crest_base.rows(), 1);

    // 5. transfer points to polydata
    // srep_poly is supposed to form a mesh grid connecting skeletal points
    vtkSmartPointer<vtkPolyData>  srep_poly       = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints>    skeletal_sheet  = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> skeletal_mesh   = vtkSmartPointer<vtkCellArray>::New();

    vtkSmartPointer<vtkPolyData>  upSpokes_poly      = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints>    upSpokes_pts       = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> upSpokes_lines     = vtkSmartPointer<vtkCellArray>::New();

    vtkSmartPointer<vtkPolyData>  downSpokes_poly      = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints>    downSpokes_pts       = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> downSpokes_lines     = vtkSmartPointer<vtkCellArray>::New();

    // TODO:crest spokes should be a little off the inner spokes
    vtkSmartPointer<vtkPolyData>  crestSpokes_poly      = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints>    crestSpokes_pts       = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> crestSpokes_lines     = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkPolyData> foldCurve_poly         = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints>    foldCurve_pts         = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> fold_curve            = vtkSmartPointer<vtkCellArray>::New();

    skeletal_sheet->SetDataTypeToDouble();
    upSpokes_pts->SetDataTypeToDouble();
    downSpokes_pts->SetDataTypeToDouble();
    crestSpokes_pts->SetDataTypeToDouble();

    for(int i = 0; i < nRows * nCols; ++i)
    {
        // skeletal points
        double mx = transformed_skeletal_points(i,0);
        double my = transformed_skeletal_points(i,1);
        double mz = transformed_skeletal_points(i,2);
        int id0 = upSpokes_pts->InsertNextPoint(mx,my, mz);

        double bx_up = transformed_up_pdm(i, 0);
        double by_up = transformed_up_pdm(i, 1);
        double bz_up = transformed_up_pdm(i, 2);
        int id1 = upSpokes_pts->InsertNextPoint(bx_up, by_up, bz_up);

        // form up spokes
        vtkSmartPointer<vtkLine> up_arrow = vtkSmartPointer<vtkLine>::New();
        up_arrow->GetPointIds()->SetId(0, id0);
        up_arrow->GetPointIds()->SetId(1, id1);
        upSpokes_lines->InsertNextCell(up_arrow);

        // form down spokes
        int id2 = downSpokes_pts->InsertNextPoint(mx, my, mz);
        double bx_down = transformed_down_pdm(i,0);
        double by_down = transformed_down_pdm(i,1);
        double bz_down = transformed_down_pdm(i,2);
        int id3 = downSpokes_pts->InsertNextPoint(bx_down,by_down,bz_down);

        vtkSmartPointer<vtkLine> down_arrow = vtkSmartPointer<vtkLine>::New();
        down_arrow->GetPointIds()->SetId(0, id2);
        down_arrow->GetPointIds()->SetId(1, id3);
        downSpokes_lines->InsertNextCell(down_arrow);

    }
    // display up spokes
    upSpokes_poly->SetPoints(upSpokes_pts);
    upSpokes_poly->SetLines(upSpokes_lines);
    AddModelNodeToScene(upSpokes_poly, "up spokes", true, 0, 1, 1);

    // display down spokes
    downSpokes_poly->SetPoints(downSpokes_pts);
    downSpokes_poly->SetLines(downSpokes_lines);
    AddModelNodeToScene(downSpokes_poly, "down spokes", true, 1, 0, 1);

    // deal with skeletal mesh
    for(int i = 0; i < nRows * nCols; ++i)
    {
        double mx = transformed_skeletal_points(i, 0);
        double my = transformed_skeletal_points(i, 1);
        double mz = transformed_skeletal_points(i, 2);
        int current_id = skeletal_sheet->InsertNextPoint(mx, my, mz);
        int current_row = floor(i / nRows);
        int current_col = i - current_row * nRows;
        if(current_col >= 0 && current_row >= 0
                && current_row < nRows-1 && current_col < nCols - 1)
        {
            vtkSmartPointer<vtkQuad> quad = vtkSmartPointer<vtkQuad>::New();
            quad->GetPointIds()->SetId(0, current_id);
            quad->GetPointIds()->SetId(1, current_id + nCols);
            quad->GetPointIds()->SetId(2, current_id + nCols + 1);
            quad->GetPointIds()->SetId(3, current_id + 1);
            skeletal_mesh->InsertNextCell(quad);
        }
    }
    srep_poly->SetPoints(skeletal_sheet);
    srep_poly->SetPolys(skeletal_mesh);
    AddModelNodeToScene(srep_poly, "skeletal mesh", true, 0, 0, 0);

    // deal with crest spokes
    for(int i = 0; i < nCrestPoints; ++i)
    {
        // tail point
        double cx_t = transformed_crest_base(i, 0);
        double cy_t = transformed_crest_base(i, 1);
        double cz_t = transformed_crest_base(i, 2);
        // head point (_b means boundary)
        double cx_b = transformed_crest_pdm(i, 0);
        double cy_b = transformed_crest_pdm(i, 1);
        double cz_b = transformed_crest_pdm(i, 2);

        if(shift > 0)
        {
            double shift_x = (cx_b - cx_t) * shift;
            double shift_y = (cy_b - cy_t) * shift;
            double shift_z = (cz_b - cz_t) * shift;

            cx_t += shift_x;
            cy_t += shift_y;
            cz_t += shift_z;
        }

        int id0 = crestSpokes_pts->InsertNextPoint(cx_t, cy_t, cz_t);
        int id1 = crestSpokes_pts->InsertNextPoint(cx_b, cy_b, cz_b);

        vtkSmartPointer<vtkLine> crest_arrow = vtkSmartPointer<vtkLine>::New();
        crest_arrow->GetPointIds()->SetId(0, id0);
        crest_arrow->GetPointIds()->SetId(1, id1);
        crestSpokes_lines->InsertNextCell(crest_arrow);


    }
    crestSpokes_poly->SetPoints(crestSpokes_pts);
    crestSpokes_poly->SetLines(crestSpokes_lines);
    AddModelNodeToScene(crestSpokes_poly, "crest spokes", true, 1, 0, 0);

    // deal with fold curve
    for(int i = 0; i < nCrestPoints; ++i)
    {
        double cx_t = transformed_crest_base(i, 0);
        double cy_t = transformed_crest_base(i, 1);
        double cz_t = transformed_crest_base(i, 2);
        double cx_b = transformed_crest_pdm(i, 0);
        double cy_b = transformed_crest_pdm(i, 1);
        double cz_b = transformed_crest_pdm(i, 2);

        if(shift > 0)
        {
            double shift_x = (cx_b - cx_t) * shift;
            double shift_y = (cy_b - cy_t) * shift;
            double shift_z = (cz_b - cz_t) * shift;

            cx_t += shift_x;
            cy_t += shift_y;
            cz_t += shift_z;
        }
        int id0 = foldCurve_pts->InsertNextPoint(cx_t, cy_t, cz_t);

        if(id0 > 0 && i < nCols)
        {
            // first row
            vtkSmartPointer<vtkLine> fold_seg = vtkSmartPointer<vtkLine>::New();
            fold_seg->GetPointIds()->SetId(0, id0-1);
            fold_seg->GetPointIds()->SetId(1, id0);
            fold_curve->InsertNextCell(fold_seg);
        }

        if(i > nCols && i < nCols + 2*(nRows-2) + 1 && (i-nCols) % 2 == 1)
        {
            // right side of crest
            vtkSmartPointer<vtkLine> fold_seg = vtkSmartPointer<vtkLine>::New();
            fold_seg->GetPointIds()->SetId(0, id0-2);
            fold_seg->GetPointIds()->SetId(1, id0);
            fold_curve->InsertNextCell(fold_seg);
        }
        if(i > nCols && i < nCols + 2*(nRows-2) + 1 && (i-nCols) % 2 == 0)
        {
            // part of left side
            vtkSmartPointer<vtkLine> fold_seg = vtkSmartPointer<vtkLine>::New();
            fold_seg->GetPointIds()->SetId(0, id0-2);
            fold_seg->GetPointIds()->SetId(1, id0);
            fold_curve->InsertNextCell(fold_seg);
        }

        if(i == nCols)
        {
            // remaining part of left side
            vtkSmartPointer<vtkLine> fold_seg = vtkSmartPointer<vtkLine>::New();
            fold_seg->GetPointIds()->SetId(0, 0);
            fold_seg->GetPointIds()->SetId(1, id0);
            fold_curve->InsertNextCell(fold_seg);
        }
        if(i > nCols + 2*(nRows-2))
        {
            //bottom side
            vtkSmartPointer<vtkLine> fold_seg = vtkSmartPointer<vtkLine>::New();
            fold_seg->GetPointIds()->SetId(0, id0-1);
            fold_seg->GetPointIds()->SetId(1, id0);
            fold_curve->InsertNextCell(fold_seg);
        }
        if(i == nCrestPoints - 1)
        {
            // bottome right
            vtkSmartPointer<vtkLine> fold_seg = vtkSmartPointer<vtkLine>::New();
            fold_seg->GetPointIds()->SetId(0, id0-nCols);
            fold_seg->GetPointIds()->SetId(1, id0);
            fold_curve->InsertNextCell(fold_seg);
        }
    }
    foldCurve_poly->SetPoints(foldCurve_pts);
    foldCurve_poly->SetLines(fold_curve);
    AddModelNodeToScene(foldCurve_poly, "fold curve", true, 1, 1, 0);

    return 0;
}

void vtkSlicerSkeletalRepresentationInitializerLogic::HideNodesByNameByClass(const std::string & nodeName, const std::string &className)
{
    std::cout << "node name:" << nodeName << std::endl;
    std::cout << "class name:" << className << std::endl;
    std::vector<vtkMRMLNode*> vectModelNodes;
    vtkSmartPointer<vtkCollection> modelNodes = this->GetMRMLScene()->GetNodesByClassByName(className.c_str(), nodeName.c_str());
    modelNodes->InitTraversal();
    for(int i = 0; i < modelNodes->GetNumberOfItems(); i++)
    {
        vtkSmartPointer<vtkMRMLModelNode> thisModelNode = vtkMRMLModelNode::SafeDownCast(modelNodes->GetNextItemAsObject());
        vtkSmartPointer<vtkMRMLModelDisplayNode> displayNode;
        displayNode = thisModelNode->GetModelDisplayNode();
        if(displayNode == NULL)
        {
            continue;
        }

        displayNode->SetVisibility(0);

    }

}

int vtkSlicerSkeletalRepresentationInitializerLogic::InklingFlow(const std::string &filename, double dt, double smooth_amount, int max_iter, int freq_output, double /*threshold*/)
{
    //std::cout << threshold << std::endl;
    std::cout << filename << std::endl;
    std::cout << dt << std::endl;
    std::cout << smooth_amount << std::endl;
    std::cout << max_iter << std::endl;
    std::cout << freq_output << std::endl;

    char tempFileName[MAX_FILE_NAME];
    sprintf(tempFileName, "%s/temp_output.vtk", this->GetApplicationLogic()->GetTemporaryPath());

    vtkSmartPointer<vtkPolyDataReader> reader =
        vtkSmartPointer<vtkPolyDataReader>::New();
    reader->SetFileName(tempFileName);
    reader->Update();

    vtkSmartPointer<vtkPolyData> mesh =
        vtkSmartPointer<vtkPolyData>::New();
    mesh = reader->GetOutput();

    vtkSmartPointer<vtkMassProperties> mass_filter =
        vtkSmartPointer<vtkMassProperties>::New();
    mass_filter->SetInputData(mesh);
    mass_filter->Update();
    double original_volume = mass_filter->GetVolume();

    int iter = 0;
    double tolerance = 0.05;
    double q = 1.0;

    while(q > tolerance && iter < max_iter)
    {
        // smooth filter
        vtkSmartPointer<vtkWindowedSincPolyDataFilter> smooth_filter =
            vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
        smooth_filter->SetPassBand(smooth_amount);
        smooth_filter->NonManifoldSmoothingOn();
        smooth_filter->NormalizeCoordinatesOn();
        smooth_filter->SetNumberOfIterations(20);
        smooth_filter->FeatureEdgeSmoothingOff();
        smooth_filter->BoundarySmoothingOff();
        smooth_filter->SetInputData(mesh);
        smooth_filter->Update();
        if(smooth_amount > 0) {
            mesh = smooth_filter->GetOutput();
        }

        // normal filter
        vtkSmartPointer<vtkPolyDataNormals> normal_filter =
            vtkSmartPointer<vtkPolyDataNormals>::New();
        normal_filter->SplittingOff();
        normal_filter->ComputeCellNormalsOff();
        normal_filter->ComputePointNormalsOn();
        normal_filter->SetInputData(mesh);
        normal_filter->Update();
        vtkDataArray* N = normal_filter->GetOutput()->GetPointData()->GetNormals();
        if(N == NULL) {
            std::cerr << "error in getting normals" << std::endl;
            return EXIT_FAILURE;
        }

        // mean curvature filter
        vtkSmartPointer<vtkCurvatures> curvature_filter =
            vtkSmartPointer<vtkCurvatures>::New();
        curvature_filter->SetCurvatureTypeToMean();
        curvature_filter->SetInputData(mesh);
        curvature_filter->Update();

        vtkSmartPointer<vtkDoubleArray> H =
            vtkDoubleArray::SafeDownCast(curvature_filter->GetOutput()->GetPointData()->GetArray("Mean_Curvature"));


        if(H == NULL) {
            std::cerr << "error in getting mean curvature" << std::endl;
            return EXIT_FAILURE;
        }

        curvature_filter->SetCurvatureTypeToGaussian();
        curvature_filter->Update();
        vtkSmartPointer<vtkDoubleArray> K =
            vtkDoubleArray::SafeDownCast(curvature_filter->GetOutput()->GetPointData()->GetArray("Gauss_Curvature"));

        if(K == NULL) {
            std::cerr << "error in getting Gaussian curvature" << std::endl;
            return EXIT_FAILURE;
        }

        curvature_filter->SetCurvatureTypeToMaximum();
        curvature_filter->Update();
        vtkSmartPointer<vtkDoubleArray> MC =
            vtkDoubleArray::SafeDownCast(curvature_filter->GetOutput()->GetPointData()->GetArray("Maximum_Curvature"));

        if(MC == NULL) {
            std::cerr << "error in getting max curvature" << std::endl;
            return EXIT_FAILURE;
        }

        curvature_filter->SetCurvatureTypeToMinimum();
        curvature_filter->Update();
        vtkSmartPointer<vtkDoubleArray> MinC =
            vtkDoubleArray::SafeDownCast(curvature_filter->GetOutput()->GetPointData()->GetArray("Minimum_Curvature"));
        if(MinC == NULL)
        {
            std::cout << "error in getting min curvature" << std::endl;
            return -1;
        }

        // perform the flow
        vtkSmartPointer<vtkPoints> points = mesh->GetPoints();
        double maxVal = -10000.0;
        double maxIndex = -1;
        for(int i = 0; i < points->GetNumberOfPoints(); ++i) {
            double p[3];
            points->GetPoint(i, p);
            double curr_N[3];
            N->GetTuple(i, curr_N);
            double curr_H = H->GetValue(i);
            //double curr_K = K->GetValue(i);
            double curr_max = MC->GetValue(i);
            double curr_min = MinC->GetValue(i);

            double delta = 0.0;
            double diffK = curr_max - curr_min;
            delta = dt * curr_H; // * 1 / (diffK * diffK);
            diffK = abs(diffK);

            if(diffK > maxVal)
            {
                maxVal = diffK;
                maxIndex = i;
            }
            if(curr_max >= 0 && curr_min >= 0)
            {
                delta = dt * curr_max;
            }
            else if(curr_max < 0 && curr_min < 0)
            {
                delta = dt * curr_min;
            }
            else
            {
                delta = dt * curr_H;
            }
            for(int idx = 0; idx < 3; ++idx)
            {
                p[idx] -= delta * curr_N[idx]; //dt * curr_H * curr_N[idx];
            }
            points->SetPoint(i, p);
        }
        // std::cout << "min diff^2:" << maxVal << std::endl;
        // std::cout << "min position:" << maxIndex << std::endl;
        points->Modified();

        mass_filter->SetInputData(mesh);
        mass_filter->Update();
        double curr_volume = mass_filter->GetVolume();

        for(int i = 0; i < points->GetNumberOfPoints(); ++i) {
            double p[3];
            points->GetPoint(i, p);
            for(int j = 0; j < 3; ++j) {
                p[j] *= std::pow( original_volume / curr_volume , 1.0 / 3.0 );
            }
        }
        points->Modified();

        double testRender[3];
        points->GetPoint(maxIndex, testRender);
//        AddPointToScene(testRender[0], testRender[1], testRender[2], 13); // sphere3D 

        // TODO: best fitting ellipsoid
        HideNodesByNameByClass("output_inkling","vtkMRMLModelNode");

        // then add this new intermediate result

//        vtkSmartPointer<vtkPolyDataWriter> writer =
//            vtkSmartPointer<vtkPolyDataWriter>::New();
//        writer->SetInputData(mesh);
//        writer->SetFileName("temp_output.vtk");
//        writer->Update();

//        char modelName[128];
//        sprintf(modelName, "output_inkling");
//        AddModelNodeToScene(mesh, modelName, true);
         if((iter +1) % freq_output == 0)
         {
             char modelName[128];
             sprintf(modelName, "output_inkling#%04d", iter+1);
             AddModelNodeToScene(mesh, modelName, false);
         }

        q -= 0.0001;
        iter++;
    }

    return 1;

}

void vtkSlicerSkeletalRepresentationInitializerLogic::AddPointToScene(double x, double y, double z, int glyphType, double r, double g, double b)
{
    std::cout << "AddPointToScene: parameters:" << x << ", y:" << y << ", z:" << z << std::endl;
    vtkMRMLScene *scene = this->GetMRMLScene();
    if(!scene)
    {
        vtkErrorMacro(" Invalid scene");
        return;
    }

    // node which controls display properties
    vtkSmartPointer<vtkMRMLMarkupsDisplayNode> displayNode;
    displayNode = vtkSmartPointer<vtkMRMLMarkupsDisplayNode>::New();
//    this->SetDisplayNodeToDefaults(displayNode);
    displayNode->SetGlyphScale(1.10);
    displayNode->SetTextScale(3.40);
    displayNode->SetSelectedColor(r, g, b);

    displayNode->SetGlyphType(glyphType); // 13: sphere3D
    scene->AddNode(displayNode);

    // model node
    vtkSmartPointer<vtkMRMLMarkupsFiducialNode> fidNode;

    fidNode = vtkSmartPointer<vtkMRMLMarkupsFiducialNode>::New();
    if(fidNode == NULL)
    {
        vtkErrorMacro("fidNode is NULL");
        return;
    }
    fidNode->SetAndObserveDisplayNodeID(displayNode->GetID());
    fidNode->SetLocked(true);
    fidNode->SetName("Hi");
    scene->AddNode(fidNode);

    fidNode->AddFiducial(x, y, z);

}

int vtkSlicerSkeletalRepresentationInitializerLogic::BackwardFlow()
{
    // 1. compute pairwise TPS

    // 2. generate s-rep for ellipsoid

    // 3. run applyTPS
    return 0;
}

int vtkSlicerSkeletalRepresentationInitializerLogic::DummyBackwardFlow(std::string& output)
{
    std::string tempDir(this->GetApplicationLogic()->GetTemporaryPath());
    tempDir += "/srep.m3d";
    output = tempDir;
    return 0;
}

int vtkSlicerSkeletalRepresentationInitializerLogic::GenerateSrep(std::string& output)
{
    std::string tempDir(this->GetApplicationLogic()->GetTemporaryPath());
    output = tempDir +  "/ellipsoid.m3d";

    return 0;
}
