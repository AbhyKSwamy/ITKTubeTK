/*=========================================================================

Library:   TubeTK/VTree

Authors: Stephen Aylward, Julien Jomier, and Elizabeth Bullitt

Original implementation:
Copyright University of North Carolina, Chapel Hill, NC, USA.

Revised implementation:
Copyright Kitware Inc., Carrboro, NC, USA.

All rights reserved.

Licensed under the Apache License, Version 2.0 ( the "License" );
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

=========================================================================*/
#ifndef __itkRadiusExtractor_txx
#define __itkRadiusExtractor_txx

#include <itkMinimumMaximumImageFilter.h>

#include "itkTubeMath.h"
#include "itkMatrixMath.h"

#include "itkRadiusExtractor.h"

namespace itk
{

/** Define the Medialness Function */
template < class ImageT >
class RadiusExtractorMedialnessFunc : public UserFunc<int, double>
{
  public:

    typedef itk::VesselTubeSpatialObject< ImageT::ImageDimension >
                                                             TubeType;
    typedef typename TubeType::TubePointType                 TubePointType;


    RadiusExtractorMedialnessFunc(
      RadiusExtractor< ImageT > * newRadiusExtractor,
      double newMedialnessScaleStep )
      {
      m_RadiusExtractor = newRadiusExtractor;
      m_MedialnessScaleStep = newMedialnessScaleStep;
      };

    void SetKernelArray( std::vector<TubePointType> * newKernelArray )
      {
      m_KernelArray = newKernelArray;
      };

    void SetMedialnessScaleStep( double newMedialnessScaleStep )
      {
      m_MedialnessScaleStep = newMedialnessScaleStep;
      };

    const double & value( const int & x )
      {
      double bness = 0;

      m_RadiusExtractor->ComputeMeasuresInKernelArray(
        *m_KernelArray, x*m_MedialnessScaleStep, m_Value, bness, false );
      // std::cout << "Optimizer: point = " << x*m_MedialnessScaleStep
        // << " value = " << m_Value << std::endl;

      return m_Value;
      };

    std::vector< TubePointType > * GetKernelArray( void  )
      {
      return m_KernelArray;
      };

    RadiusExtractor< ImageT > * GetRadiusExtractor( void  )
      {
      return m_RadiusExtractor;
      }

  private:

    std::vector<TubePointType>             * m_KernelArray;

    RadiusExtractor< ImageT >              * m_RadiusExtractor;

    double                                   m_Value;

    double                                   m_MedialnessScaleStep;

};

/** Constructor */
template<class TInputImage>
RadiusExtractor<TInputImage>
::RadiusExtractor()
{;
  m_Debug = false;
  m_Verbose = true;

  m_Image = NULL;
  m_ImageXMin.Fill( 0 );
  m_ImageXMax.Fill( -1 );

  m_DataOp = BlurImageFunction<ImageType>::New();
  m_DataOp->SetScale( 1.0 );
  m_DataOp->SetExtent( 1.1 );
  m_DataMin = 0;
  m_DataMax = -1;

  m_NumKernelPoints = 5;
  m_KernelPointSpacing = 10;
  m_Radius0 = 1.0;
  m_ExtractRidge = true;

  m_ThreshMedialness = 0.04;       // 0.015; larger = harder
  m_ThreshMedialnessStart = 0.01;

  if( ImageDimension == 2 )
    {
    m_KernNumDirs = 2;
    m_KernX.set_size( ImageDimension, m_KernNumDirs );
    m_KernX(0, 0) = 1;
    m_KernX(1, 0) = 0;
    m_KernX(0, 1) = -1;
    m_KernX(1, 1) = 0;
    }
  else if( ImageDimension == 3 )
    {
    m_KernNumDirs = 8;
    m_KernX.set_size( ImageDimension, m_KernNumDirs );
    int dir = 0;
    for( double theta=0; theta<vnl_math::pi-vnl_math::pi/8;
      theta+=( double )( vnl_math::pi/4 ) )
      {
      m_KernX(0, dir) = cos( theta );
      m_KernX(1, dir) = sin( theta );
      m_KernX(2, dir) = 0;
      ++dir;
      m_KernX(0, dir) = -cos( theta );
      m_KernX(1, dir) = -sin( theta );
      m_KernX(2, dir) = 0;
      ++dir;
      }
    }
  else
    {
    std::cerr
      << "Error: Radius estimation only supports 2 & 3 dimensions."
      << std::endl;
    throw "Error: Radius estimation only supports 2 & 3 dimensions.";
    }

  m_MedialnessScaleStep = 0.25;

  m_MedialnessFunc = new RadiusExtractorMedialnessFunc<TInputImage>(
    this, m_MedialnessScaleStep );

  m_MedialnessOpt.tolerance( 0.001 / m_MedialnessScaleStep );
  //TMI:0.001 // 0.005 - 0.025

  m_MedialnessOpt.xStep( 0.25 / m_MedialnessScaleStep );
  // TMI: 0.1 // Org: 0.49; Good: 0.25; NEXT: 1.0

  m_MedialnessOpt.searchForMin( false );

  m_MedialnessOptSpline = new SplineApproximation1D( m_MedialnessFunc,
    &m_MedialnessOpt );

  m_MedialnessOptSpline->xMin( 0.5 / m_MedialnessScaleStep );
  m_MedialnessOptSpline->xMax( 20 / m_MedialnessScaleStep );


  m_IdleCallBack = NULL;
  m_StatusCallBack = NULL;
}

/** Destructor */
template<class TInputImage>
RadiusExtractor<TInputImage>
::~RadiusExtractor()
{
  if( m_MedialnessFunc != NULL )
    {
    delete m_MedialnessFunc;
    }
  m_MedialnessFunc = NULL;
}


/** Set the scale factor */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::SetScale( double scale )
{
  m_Scale = scale;
  m_DataOp->SetScale( scale );
}


/** Set the extent factor */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::SetExtent( double extent )
{
  m_Extent = extent;
  m_DataOp->SetExtent( extent );
}


/** Set Radius Min */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::SetRadiusMin( double radiusMin )
{
  if( radiusMin < 0.5 )
    {
    radiusMin = 0.5;
    }
  m_RadiusMin = radiusMin;
  m_MedialnessOptSpline->xMin( m_RadiusMin / m_MedialnessScaleStep );
}

/** Set Radius Max */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::SetRadiusMax( double radiusMax )
{
  this->m_RadiusMax = radiusMax;
  m_MedialnessOptSpline->xMax( this->m_RadiusMax / m_MedialnessScaleStep );
}

/** Get the medialness operator */
template<class TInputImage>
OptBrent1D &
RadiusExtractor<TInputImage>
::GetMedialnessOptimizer( void )
{
  return & m_MedialnessOpt;
}

/** Get the medialness operator */
template<class TInputImage>
SplineApproximation1D &
RadiusExtractor<TInputImage>
::GetMedialnessOptimizerSpline( void )
{
  return m_MedialnessOptSpline;
}

/** Set the input image */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::SetInputImage( typename ImageType::Pointer inputImage  )
{
  m_Image = inputImage;

  if( m_Image )
    {
    m_DataOp->SetInputImage( m_Image );
    typedef MinimumMaximumImageFilter<ImageType> MinMaxFilterType;
    typename MinMaxFilterType::Pointer minMaxFilter =
      MinMaxFilterType::New();
    minMaxFilter->SetInput( m_Image );
    minMaxFilter->Update();
    m_DataMin = minMaxFilter->GetMinimum();
    m_DataMax = minMaxFilter->GetMaximum();

    m_ImageXMin = m_Image->GetLargestPossibleRegion().GetIndex();
    for( unsigned int i=0; i<ImageDimension; i++ )
      {
      m_ImageXMax[i] = m_ImageXMin[i] +
        m_Image->GetLargestPossibleRegion().GetSize()[i] - 1;
      }

    if( m_Debug )
      {
      std::cout << "RadiusExtractor: SetInputImage: Minimum = "
        << m_DataMin << std::endl;
      std::cout << "RadiusExtractor: SetInputImage: Maximum = "
        << m_DataMax << std::endl;
      }
    }
}



/**
 * Compute the medialness at a point
 */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::ComputeMeasuresAtPoint( TubePointType & pnt, double pntR,
  double & mness, double & bness, bool doBNess )
{
  if( pntR < m_MedialnessOptSpline->xMin() * m_MedialnessScaleStep )
    {
    pntR = m_MedialnessOptSpline->xMin() * m_MedialnessScaleStep;
    }

  if( pntR > m_MedialnessOptSpline->xMax() * m_MedialnessScaleStep )
    {
    pntR = m_MedialnessOptSpline->xMax() * m_MedialnessScaleStep;
    }

  MatrixType n( ImageDimension, ImageDimension-1 );

  // Verify the normal directions stored in the point are actually
  //   normal to the point's tangent direction.
  double dotP = vnl_math_abs( dot_product( pnt.GetNormal1().GetVnlVector(),
    pnt.GetTangent().GetVnlVector() ) );
  if( ImageDimension == 3 )
    {
    dotP += vnl_math_abs( dot_product( pnt.GetNormal2().GetVnlVector(),
      pnt.GetTangent().GetVnlVector() ) );
    }
  double len = pnt.GetNormal1().GetNorm();
  if( dotP < 0.001 && vnl_math_abs( 1 - len ) < 0.01 )
    {
    n.set_column( 0, pnt.GetNormal1().GetVnlVector() );
    if( ImageDimension == 3 )
      {
      n.set_column( 1, pnt.GetNormal2().GetVnlVector() );
      }
    }
  else
    {
    // If the point's normals, aren't normal, then create new normals.
    //   However, given only one point, we cannot compute the tube's
    //   local Frenet frame.   Ideally the user should call
    //   ComputeTangents on the tube prior to calling this function
    //   to avoid this situation.  If inconsistent normals are used,
    //   branchness computations suffer due to normal flipping.
    std::cout
      << "Warning: Point normals invalid. Recomputing. Frenet frame lost."
      << std::endl;
    if( m_Debug )
      {
      std::cout << "   DotProd = " << dotP << " and Norm = " << len
        << std::endl;
      }
    n.set_column( 0,
      GetOrthogonalVector( pnt.GetTangent().GetVnlVector() )  );
    n.get_column( 0 ).normalize();
    if( ImageDimension == 3 )
      {
      n.set_column( 1,
        GetCrossVector( pnt.GetTangent().GetVnlVector(),
          n.get_column( 0 ) )  );
      n.get_column( 1 ).normalize();
      }
    }

  VectorType kernPos( m_KernNumDirs );
  VectorType kernNeg( m_KernNumDirs );
  VectorType kernBrn( m_KernNumDirs );
  kernPos.fill( 0 );
  kernNeg.fill( 0 );
  kernBrn.fill( 0 );
  double kernPosCnt = 0;
  double kernNegCnt = 0;
  double kernBrnCnt = 0;

  if( m_Debug )
    {
    std::cout << "Compute values at point" << std::endl;
    }
  this->ComputeValuesInKernel( pnt, pntR, n, kernPos, kernPosCnt,
    kernNeg, kernNegCnt, kernBrn, kernBrnCnt, doBNess );

  this->ComputeMeasuresInKernel( pntR, kernPos, kernPosCnt,
    kernNeg, kernNegCnt, kernBrn, kernBrnCnt, mness, bness, doBNess );
  pnt.SetMedialness( mness );
  if( doBNess )
    {
    pnt.SetBranchness( bness );
    }
}

/** Compute the medialness at a kernel */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::ComputeMeasuresInKernelArray( KernArrayType & kernArray,
  double pntR, double & mness, double & bness, bool doBNess )
{
  unsigned int len = kernArray.size();

  if( len == 0 )
    {
    mness = 0;
    bness = 0;
    return;
    }
  else if( len == 1 )
    {
    this->ComputeMeasuresAtPoint( kernArray[0], pntR, mness, bness,
      doBNess );
    }

  unsigned int mid = ( len - 1 ) / 2;

  double wTot = 0;
  VectorType w( len );
  for( unsigned int i=0; i<len; i++ )
    {
    w[i] = 1.0 - vnl_math_abs( (double)i - (double)mid ) / ( 2.0 * mid );
    wTot += w[i];
    }
  for( unsigned int i=0; i<len; i++ )
    {
    w[i] /= wTot;
    }

  if( pntR < m_MedialnessOptSpline->xMin() * m_MedialnessScaleStep )
    {
    pntR = m_MedialnessOptSpline->xMin() * m_MedialnessScaleStep;
    }

  if( pntR > m_MedialnessOptSpline->xMax() * m_MedialnessScaleStep )
    {
    pntR = m_MedialnessOptSpline->xMax() * m_MedialnessScaleStep;
    }

  if( m_Debug )
    {
    std::cout << "RadiusExtractor: MedialnessInKern: pntR = "
      << pntR << " : size = " << kernArray.size()
      << std::endl;
    }

  VectorType kernPosTot( m_KernNumDirs );
  VectorType kernNegTot( m_KernNumDirs );
  VectorType kernBrnTot( m_KernNumDirs );
  kernPosTot.fill( 0 );
  kernNegTot.fill( 0 );
  kernBrnTot.fill( 0 );
  double kernPosCntTot = 0;
  double kernNegCntTot = 0;
  double kernBrnCntTot = 0;

  VectorType kernPos( m_KernNumDirs );
  VectorType kernNeg( m_KernNumDirs );
  VectorType kernBrn( m_KernNumDirs );
  kernPos.fill( 0 );
  kernNeg.fill( 0 );
  kernBrn.fill( 0 );
  double kernPosCnt = 0;
  double kernNegCnt = 0;
  double kernBrnCnt = 0;

  MatrixType norms( ImageDimension, ImageDimension-1 );
  norms.set_column(0, kernArray[mid].GetNormal1().GetVnlVector() );
  if( ImageDimension > 2 )
    {
    norms.set_column(1, kernArray[mid].GetNormal2().GetVnlVector() );
    }

  // With the coordinate frame defined, compute medialness for other points
  typename std::vector<TubePointType>::iterator pnt = kernArray.begin();
  for( unsigned int i=0; i<len; ++i )
    {
    this->ComputeValuesInKernel( *pnt, pntR, norms, kernPos,
      kernPosCnt, kernNeg, kernNegCnt, kernBrn, kernBrnCnt,
      doBNess );
    for( unsigned int d=0; d<m_KernNumDirs; d++ )
      {
      kernPosTot[d] += w[i] * kernPos[d];
      kernNegTot[d] += w[i] * kernNeg[d];
      kernBrnTot[d] += w[i] * kernBrn[d];
      }
    kernPosCntTot += w[i] * kernPosCnt;
    kernNegCntTot += w[i] * kernNegCnt;
    kernBrnCntTot += w[i] * kernBrnCnt;
    ++pnt;
    }

  for( unsigned int i = 0; i < m_KernNumDirs; i++ )
    {
    if( kernPosCntTot > 0 )
      {
      kernPosTot[i] /= kernPosCntTot;
      }
    if( kernNegCntTot > 0 )
      {
      kernNegTot[i] /= kernNegCntTot;
      }
    if( kernBrnCntTot > 0 )
      {
      kernBrnTot[i] /= kernBrnCntTot;
      }
    }

  mness = 0;
  bness = 0;
  this->ComputeMeasuresInKernel( pntR, kernPosTot, kernPosCntTot,
    kernNegTot, kernNegCntTot, kernBrnTot, kernBrnCntTot,
    mness, bness, doBNess );
  if( m_Debug )
    {
    std::cout << "At radius = " << pntR << " medialness = " << mness
      << std::endl << std::endl;
    }
}

/** Compute the Optimal scale */
template<class TInputImage>
bool
RadiusExtractor<TInputImage>
::ComputeOptimalRadiusAtPoint( TubePointType & pnt, double & r0,
  double rMin, double rMax, double rStep, double rTolerance )
{
  TubePointType tmpPnt;
  ITKPointType x;

  KernArrayType kernArray;

  for( unsigned int i=0; i<ImageDimension; i++ )
    {
    x[i] = pnt.GetPosition()[i] - pnt.GetTangent()[i];
    }
  tmpPnt.SetPosition( x );
  tmpPnt.SetRadius( r0 );
  tmpPnt.SetTangent( pnt.GetTangent() );
  tmpPnt.SetNormal1( pnt.GetNormal1() );
  tmpPnt.SetNormal2( pnt.GetNormal2() );
  kernArray.push_back( tmpPnt );

  ITKPointType x1 = pnt.GetPosition();
  tmpPnt.SetPosition( x1 );
  tmpPnt.SetRadius( r0 );
  tmpPnt.SetTangent( pnt.GetTangent() );
  tmpPnt.SetNormal1( pnt.GetNormal1() );
  tmpPnt.SetNormal2( pnt.GetNormal2() );
  kernArray.push_back( tmpPnt );

  for( unsigned int i=0; i<ImageDimension; i++ )
    {
    x1[i] = pnt.GetPosition()[i] + pnt.GetTangent()[i];
    }
  tmpPnt.SetPosition( x1 );
  tmpPnt.SetRadius( r0 );
  tmpPnt.SetTangent( pnt.GetTangent() );
  tmpPnt.SetNormal1( pnt.GetNormal1() );
  tmpPnt.SetNormal2( pnt.GetNormal2() );
  kernArray.push_back( tmpPnt );

  double mness = 0;

  if( rMin < 0.5 )
    {
    rMin = 0.5;
    }
  double tempXMin = m_MedialnessOptSpline->xMin();
  m_MedialnessOptSpline->xMin( rMin / m_MedialnessScaleStep );

  double tempXMax = m_MedialnessOptSpline->xMax();
  m_MedialnessOptSpline->xMax( rMax / m_MedialnessScaleStep );

  double tempXStep = m_MedialnessOpt.xStep();
  m_MedialnessOpt.xStep( rStep / m_MedialnessScaleStep );

  double tempTol = m_MedialnessOpt.tolerance();
  m_MedialnessOpt.tolerance( rTolerance / m_MedialnessScaleStep );

  static_cast< RadiusExtractorMedialnessFunc< TInputImage > *>(
    m_MedialnessFunc )->SetKernelArray( & kernArray );
  m_MedialnessOptSpline->newData( true );
  r0 /= m_MedialnessScaleStep;
  m_MedialnessOptSpline->extreme( &r0, &mness );
  r0 *= m_MedialnessScaleStep;

  if( m_Debug )
    {
    std::cout << "Local extreme at radius = " << r0
      << " with medialness = " << mness << std::endl << std::endl;
    }

  m_MedialnessOptSpline->xMin( tempXMin );
  m_MedialnessOptSpline->xMax( tempXMax );
  m_MedialnessOpt.xStep( tempXStep );
  m_MedialnessOpt.tolerance( tempTol );

  pnt.SetRadius( r0 );

  if( mness > m_ThreshMedialnessStart )
    {
    return true;
    }
  else
    {
    std::cout
      << "RadiusExtractor: calcOptimalScale: kernel fit insufficient"
      << std::endl;
    std::cout << "  Medialness = " << mness << " < thresh = "
      << m_ThreshMedialness << std::endl;
    return false;
    }

  return true;
}


template<class TInputImage>
void
RadiusExtractor<TInputImage>
::ComputeValuesInSubKernel( TubePointType pnt, double pntR,
  MatrixType & kernN, VectorType & kern, double & kernCnt )
{
  if( pntR < m_MedialnessOptSpline->xMin() * m_MedialnessScaleStep )
    {
    pntR = m_MedialnessOptSpline->xMin() * m_MedialnessScaleStep;
    }

  if( pntR > m_MedialnessOptSpline->xMax() * m_MedialnessScaleStep )
    {
    pntR = m_MedialnessOptSpline->xMax() * m_MedialnessScaleStep;
    }


  /*
  std::cout << "Kern pnt = " << pnt.GetPosition() << std::endl;
  for( unsigned int i=0; i<ImageDimension-1; i++ )
    {
    std::cout << "Kern N[" << i << "] = " << kernN.get_column( i )
      << std::endl;
    }
  std::cout << "Kern r = " << pntR << std::endl;
  std::cout << "Kern X = " << m_KernX << std::endl;
  */

  VectorType nodePnt;
  for( unsigned int dir=0; dir<m_KernNumDirs; dir++ )
    {
    nodePnt = pnt.GetPosition().GetVnlVector();
    for( unsigned int i=0; i<ImageDimension-1; i++ )
      {
      for( unsigned int d=0; d<ImageDimension; d++ )
        {
        nodePnt[d] += pntR * m_KernX( i, dir ) * kernN( d, i );
        }
      }

    /*
    VectorType xV = nodePnt - pnt.GetPosition().GetVnlVector();
    std::cout << "  dirX = " << m_KernX.get_column( dir ) << std::endl;
    std::cout << "  node pnt = " << nodePnt << std::endl;
    std::cout << "  dist(kernPnt, nodePnt) = " << xV.magnitude()
      << std::endl;
    xV.normalize();
    double dotP = dot_product( pnt.GetTangent().GetVnlVector(), xV );
    std::cout << "  dotp(kernPnt.tan, nodePnt.dist) = " << dotP
      << std::endl;
      */

    bool inBounds = true;
    for( unsigned int i=0; i<ImageDimension; i++ )
      {
      if( nodePnt[i] < m_ImageXMin[i] || nodePnt[i] > m_ImageXMax[i] )
        {
        inBounds = false;
        break;
        }
      }

    if( inBounds )
      {
      ContinuousIndex<double, ImageDimension> nodeCIndx;

      for( unsigned int i=0; i<ImageDimension; i++ )
        {
        nodeCIndx[i] = nodePnt[i];
        }

      double val = ( m_DataOp->EvaluateAtContinuousIndex( nodeCIndx )
        - m_DataMin ) / ( m_DataMax - m_DataMin );

      if( !m_ExtractRidge )
        {
        val = 1 - val;
        }

      if( val < 0 )
        {
        val = 0;
        }

      if( val > 1 )
        {
        val = 1;
        }

      kern[dir] += val;
      ++kernCnt;
      }
    else
      {
      kern[dir] = 0;
      }
    }
}

template<class TInputImage>
void
RadiusExtractor<TInputImage>
::ComputeValuesInKernel( TubePointType pnt, double pntR,
  MatrixType & kernN, VectorType & kernPos, double & kernPosCnt,
  VectorType & kernNeg, double & kernNegCnt,
  VectorType & kernBrn, double & kernBrnCnt, bool doBNess )
{
  if( pntR < m_MedialnessOptSpline->xMin() * m_MedialnessScaleStep )
    {
    pntR = m_MedialnessOptSpline->xMin() * m_MedialnessScaleStep;
    }

  if( pntR > m_MedialnessOptSpline->xMax() * m_MedialnessScaleStep )
    {
    pntR = m_MedialnessOptSpline->xMax() * m_MedialnessScaleStep;
    }

  MatrixType n( ImageDimension, ImageDimension-1 );

  // Verify the normal directions stored in the point are actually
  //   normal to the point's tangent direction.
  double dotP = vnl_math_abs( dot_product( pnt.GetNormal1().GetVnlVector(),
    pnt.GetTangent().GetVnlVector() ) );
  if( ImageDimension == 3 )
    {
    dotP += vnl_math_abs( dot_product( pnt.GetNormal2().GetVnlVector(),
      pnt.GetTangent().GetVnlVector() ) );
    }
  double sum = pnt.GetNormal1().GetNorm();
  if( dotP < 0.001 && vnl_math_abs( 1 - sum ) < 0.01 )
    {
    n.set_column( 0, pnt.GetNormal1().GetVnlVector() );
    if( ImageDimension == 3 )
      {
      n.set_column( 1, pnt.GetNormal2().GetVnlVector() );
      }
    }
  else
    {
    // If the point's normals, aren't normal, then create new normals.
    //   However, given only one point, we cannot compute the tube's
    //   local Frenet frame.   Ideally the user should call
    //   ComputeTangents on the tube prior to calling this function
    //   to avoid this situation.  If inconsistent normals are used,
    //   branchness computations suffer due to normal flipping.
    std::cout
      << "Warning: Point normals invalid. Recomputing. Frenet frame lost."
      << std::endl;
    if( m_Debug )
      {
      std::cout << "   DotProd = " << dotP << " and Norm = " << sum
        << std::endl;
      }
    n.set_column( 0,
      GetOrthogonalVector( pnt.GetTangent().GetVnlVector() )  );
    n.get_column( 0 ).normalize();
    // Should we set the point's normals?
    if( ImageDimension == 3 )
      {
      n.set_column( 1,
        GetCrossVector( pnt.GetTangent().GetVnlVector(),
          n.get_column( 0 ) )  );
      n.get_column( 1 ).normalize();
      }
    }

  if( m_Debug )
    {
    std::cout << "kernN0 = " << kernN.get_column( 0 ) << std::endl;
    std::cout << "kernN1 = " << kernN.get_column( 1 ) << std::endl;
    }

  VectorType n0( ImageDimension );
  n0.fill( 0 );
  for( unsigned int j=0; j<ImageDimension-1; j++ )
    {
    double dp = dot_product( kernN.get_column( 0 ),
      n.get_column( j ) );
    n0 += dp * n.get_column( j );
    }
  n0.normalize();
  if( m_Debug )
    {
    std::cout << "n0 = " << n0 << std::endl;
    }
  n.set_column( 0, n0 );
  if( ImageDimension == 3 )
    {
    n.set_column( 1,
      GetCrossVector( pnt.GetTangent().GetVnlVector(),
        n.get_column( 0 ) )  );
    n.get_column( 1 ).normalize();
    if( m_Debug )
      {
      std::cout << "n1 = " << n.get_column( 1 ) << std::endl;
      }

    double tf = dot_product( kernN.get_column( 1 ),
      n.get_column( 1 ) );
    if( tf < 0 )
      {
      n.set_column( 1, n.get_column( 1 ) * -1 );
      }
    }

  kernPos.fill( 0 );
  kernNeg.fill( 0 );
  kernBrn.fill( 0 );
  kernPosCnt = 0;
  kernNegCnt = 0;
  kernBrnCnt = 0;

  double e = 1.1;
  double f = 4.0;
  if( ( pntR / f ) * e < 0.71 )  // sqrt( 2 )/2 = 0.7071 approx = 0.71
    {
    f = ( ( pntR * e ) / 0.71 + f ) / 2;
    e = 0.71 / ( pntR / f );
    }
  if( ( pntR / f ) * e > 3.1 )
    {
    f = ( ( pntR * e ) / 3.1 + f ) / 2;
    e = 3.1 / ( pntR / f );
    }
  m_DataOp->SetScale( pntR / f );
  m_DataOp->SetExtent( e );
  double r = (f-e)/f * pntR;
  if( m_Debug )
    {
    std::cout << "Pos: opR = " << pntR/f << " opE = " << e
      << " dist = " << r << std::endl;
    }
  this->ComputeValuesInSubKernel( pnt, r, n, kernPos, kernPosCnt );
  if( kernPosCnt > 0 )
    {
    for( unsigned int i=0; i<m_KernNumDirs; i++ )
      {
      kernPos[i] /= kernPosCnt;
      }
    }

  r = (f+e) * pntR / f;
  if( m_Debug )
    {
    std::cout << "Neg: opR = " << pntR/f << " opE = " << e
      << " dist = " << r << std::endl;
    }
  this->ComputeValuesInSubKernel( pnt, r, n, kernNeg, kernNegCnt );
  if( kernNegCnt > 0 )
    {
    for( unsigned int i=0; i<m_KernNumDirs; i++ )
      {
      kernNeg[i] /= kernNegCnt;
      }
    }

  if( doBNess )
    {
    e = 1.1;
    f = 3.0;
    if( ( pntR / f ) * e < 1.1 )  // sqrt( 2 )/2 = 0.7071 approx = 0.71
      {
      f = ( ( pntR * e ) / 1.1 + f ) / 2;
      e = 1.1 / ( pntR / f );
      }
    if( ( pntR / f ) * e > 3.1 )
      {
      f = ( ( pntR * e ) / 3.1 + f ) / 2;
      e = 3.1 / ( pntR / f );
      }
    m_DataOp->SetScale( pntR / f ); // mess with this -  and r
    m_DataOp->SetExtent( e );
    r = f * pntR;
    if( m_Debug )
      {
      std::cout << "Brn: opR = " << pntR/f << " opE = " << e
        << " dist = " << r << std::endl;
      }
    this->ComputeValuesInSubKernel( pnt, r, n, kernBrn, kernBrnCnt );
    if( kernBrnCnt >= 0 )
      {
      for( unsigned int i=0; i<m_KernNumDirs; i++ )
        {
        kernBrn[i] /= kernBrnCnt;
        }
      }
    }

  int kernCnt = 0;
  for( unsigned int i=0; i<m_KernNumDirs; i++ )
    {
    if( kernNeg[i]!=0 && kernPos[i]!=0 )
      {
      kernCnt++;
      }
    }
  if( kernCnt < 2 )
    {
    std::cout << "Warning: Medialness kernel does not intersect image."
      << std::endl;
    if( m_Debug )
      {
      for( unsigned int i=0; i<m_KernNumDirs; i++ )
        {
        std::cout << i << " : " << kernNeg[i] << ", " << kernPos[i]
          << ", " << kernBrn[i] << std::endl;
        }
      }
    }
}

/** Compute kernel array */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::ComputeValuesInFullKernelArray( TubeType & tube,
  KernArrayType & kernArray,
  KernArrayTubePointIndexType & kernArrayTubePointIndex )
{
  typename KernArrayType::iterator tubeFromPnt = tube.GetPoints().begin();
  typename KernArrayType::iterator tubeToPnt = tube.GetPoints().end();
  tubeToPnt--;

  typename KernArrayType::iterator iterPnt;
  iterPnt = tubeFromPnt;

  kernArray.clear();
  kernArrayTubePointIndex.clear();

  unsigned int kernPointCount = 0;
  unsigned int tubePointCount = 0;
  while( iterPnt != tubeToPnt )
    {
    int avgCount = 0;
    TubePointType kernPnt;
    for( unsigned int j = 0;
      iterPnt != tubeToPnt && j < m_KernelPointSpacing;
      j++ )
      {
      ITKPointType tmpPoint;

      for( unsigned int id = 0; id < ImageDimension; id++ )
        {
        tmpPoint[id] = kernPnt.GetPosition()[id]
          + ( *iterPnt ).GetPosition()[id];
        }
      kernPnt.SetPosition( tmpPoint );

      double dotP = dot_product( kernPnt.GetTangent().GetVnlVector(),
        ( *iterPnt ).GetTangent().GetVnlVector() );
      if( dotP > 0 )
        {
        kernPnt.SetTangent( kernPnt.GetTangent() +
          ( *iterPnt ).GetTangent() );
        }
      else
        {
        kernPnt.SetTangent( kernPnt.GetTangent() -
          ( *iterPnt ).GetTangent() );
        }

      avgCount++;
      iterPnt++;
      tubePointCount++;
      }

    ITKPointType tmpPoint;
    for( unsigned int id = 0; id < ImageDimension; id++ )
      {
      tmpPoint[id] = kernPnt.GetPosition()[id] / avgCount;
      }
    kernPnt.SetPosition( tmpPoint );

    ITKVectorType tempVect = kernPnt.GetTangent();
    tempVect.Normalize();
    kernPnt.SetTangent( tempVect );

    kernPnt.SetRadius( 0 );

    if( iterPnt == tubeToPnt )
      {
      if( kernPointCount == 0 )
        {
        kernArray.push_back( kernPnt );
        kernArrayTubePointIndex.push_back( tubePointCount );
        kernPointCount++;
        }
      return;
      }

    kernArray.push_back( kernPnt );
    kernArrayTubePointIndex.push_back( tubePointCount );
    kernPointCount++;
    }
}


/**
 * Compute the medialness and the branchness */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::ComputeMeasuresInKernel( double pntR,
  VectorType & kernPos, double & itkNotUsed( kernPosCnt),
  VectorType & kernNeg, double & itkNotUsed( kernNegCnt),
  VectorType & kernBrn, double & itkNotUsed( kernBrnCnt),
  double & mness, double & bness, bool doBNess )
{
  int kernAvgCnt = 0;
  for( unsigned int i=0; i<m_KernNumDirs; i++ )
    {
    if( kernNeg[i] != 0 && kernPos[i] != 0 )
      {
      kernAvgCnt++;
      }
    }

  if( kernAvgCnt<2 )
    {
    mness = 0;
    bness = 0;
    return;
    }

  int kernNegMinI = -1;
  for( unsigned int i=0; i<m_KernNumDirs; i++ )
    {
    if( kernNeg[i] != 0 && kernPos[i] != 0 )
      {
      kernNegMinI = i;
      break;
      }
    }

  int kernNegMaxI = kernNegMinI;
  double kernNegMin = kernNeg[kernNegMinI];
  double kernNegMax = kernNegMin;
  double kernNegAvg = kernNegMin;
  double kernPosMin = kernPos[kernNegMinI];
  double kernPosMax = kernPosMin;
  double kernPosAvg = kernPosMin;

  for( unsigned int i = kernNegMinI+1; i < m_KernNumDirs; i++ )
    {
    if( kernNeg[i] != 0 && kernPos[i] != 0 )
      {
      if( kernNeg[i]>kernNegMax )
        {
        kernNegMax = kernNeg[i];
        kernNegMaxI = i;
        }
      if( kernNeg[i]<kernNegMin )
        {
        kernNegMin = kernNeg[i];
        kernNegMinI = i;
        }
      if( kernPos[i]>kernPosMax )
        {
        kernPosMax = kernPos[i];
        }
      if( kernPos[i]<kernPosMin )
        {
        kernPosMin = kernPos[i];
        }
      kernNegAvg += kernNeg[i];
      kernPosAvg += kernPos[i];
      }
    }

  if( fabs( kernNegMin-kernNegAvg/kernAvgCnt ) >
    fabs( kernNegMax-kernNegAvg/kernAvgCnt ) && kernAvgCnt>2 )
    {
    kernPosAvg -= kernPos[kernNegMinI];
    kernNegAvg -= kernNegMin;

    kernPosAvg += ( kernPos[kernNegMinI] + kernPosAvg / ( kernAvgCnt-1 ) )
      / 2;
    kernNegAvg += ( kernNegMin + kernNegAvg / ( kernAvgCnt-1 ) )
      / 2;
    int l, m;
    l = ( kernNegMinI+1 )%( m_KernNumDirs );
    m = ( kernNegMinI+m_KernNumDirs-1 )%( m_KernNumDirs );
    if( kernNeg[l] != 0 && kernPos[l] != 0 && kernNeg[l] < kernNeg[m] )
      {
      kernPosAvg -= kernPos[l];
      kernNegAvg -= kernNeg[l];
      kernPosAvg += ( kernPos[l] + kernPosAvg / (kernAvgCnt-1) ) / 2;
      kernNegAvg += ( kernNeg[l] + kernNegAvg / (kernAvgCnt-1) ) / 2;
      }
    else if( kernNeg[m] != 0 && kernPos[m] != 0
      && kernNeg[l] >= kernNeg[m] )
      {
      kernPosAvg -= kernPos[m];
      kernNegAvg -= kernNeg[m];
      kernPosAvg += ( kernPos[m] + kernPosAvg / (kernAvgCnt-1) ) / 2;
      kernNegAvg += ( kernNeg[m] + kernNegAvg / (kernAvgCnt-1) ) / 2;
      }
    }
  else if( kernAvgCnt>2 )
    {
    kernPosAvg -= kernPos[kernNegMaxI];
    kernNegAvg -= kernNegMax;
    kernPosAvg += ( kernPos[kernNegMaxI] + kernPosAvg / (kernAvgCnt-1) )
      / 2;
    kernNegAvg += ( kernNegMax + kernNegAvg / (kernAvgCnt-1) )
      / 2;
    int l, m;
    l = ( kernNegMaxI + 1 )%( m_KernNumDirs );
    m = ( kernNegMaxI + m_KernNumDirs - 1 )%( m_KernNumDirs );
    if( kernNeg[l] != 0 && kernPos[l] != 0 && kernNeg[l] > kernNeg[m] )
      {
      kernPosAvg -= kernPos[l];
      kernNegAvg -= kernNeg[l];
      kernPosAvg += ( kernPos[l] + kernPosAvg / (kernAvgCnt-1) ) / 2;
      kernNegAvg += ( kernNeg[l] + kernNegAvg / (kernAvgCnt-1) ) / 2;
      }
    else if( kernNeg[m] != 0 && kernPos[m] != 0
      && kernNeg[l] <= kernNeg[m] )
      {
      kernPosAvg -= kernPos[m];
      kernNegAvg -= kernNeg[m];
      kernPosAvg += ( kernPos[m] + kernPosAvg / (kernAvgCnt-1) ) / 2;
      kernNegAvg += ( kernNeg[m] + kernNegAvg / (kernAvgCnt-1) ) / 2;
      }
    }
  if( kernAvgCnt != 0 )
    {
    kernPosAvg /= kernAvgCnt;
    kernNegAvg /= kernAvgCnt;
    }
  else
    {
    kernPosAvg = 0;
    kernNegAvg = 0;
    }
  mness = ( kernPosAvg - kernNegAvg ) / ( pntR / 4 );

  if( doBNess )
    {
    unsigned int kernBrnMaxI = m_KernNumDirs;
    double kernBrnMax, kernBrnAvg;
    double kernBrnAvgCnt = 1;
    for( unsigned int i=0; i<m_KernNumDirs; i++ )
      {
      if( kernPos[i] != 0 && kernNeg[i] != 0 && kernBrn[i] != 0 )
        {
        kernBrnMaxI = i;
        break;
        }
      }

    if( kernBrnMaxI < m_KernNumDirs )
      {
      kernBrnMax = kernBrn[kernBrnMaxI];
      kernBrnAvg = kernBrnMax;

      for( unsigned int i = kernBrnMaxI+1; i<m_KernNumDirs; i++ )
        {
        if( kernPos[i] != 0 && kernNeg[i] != 0 && kernBrn[i] != 0 )
          {
          kernBrnAvg += kernBrn[i];
          kernBrnAvgCnt++;
          if( kernBrn[i] > kernBrnMax )
            {
            kernBrnMax = kernBrn[i];
            }
          }
        }

      kernBrnAvg -= kernBrnMax;
      if( ( kernBrnAvgCnt-1 ) != 0 )
        {
        kernBrnAvg /= ( kernBrnAvgCnt-1 );
        }
      else
        {
        kernBrnAvg=0;
        }

      if( ( kernPosAvg - kernBrnAvg ) != 0 )
        {
        bness = ( 3 * ( kernBrnMax - kernBrnAvg )
          / ( kernPosAvg - kernBrnAvg )
          + ( kernNegMax - kernNegAvg )
          / ( kernPosAvg - kernBrnAvg ) ) / 4;
        }
      else
        {
        bness=0;
        }

      if( bness>2 )
        {
        bness = 2;
        }
      if( bness<0 )
        {
        bness = 0;
        }
      }
    else
      {
      bness = 0;
      }
    }
  else
    {
    bness = 0;
    }
}


/** Compute Kernel radii one way */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::ComputeMeasuresInFullKernelArray( KernArrayType & kernArray,
  unsigned int kernPntStart, unsigned int kernPntEnd )
{
  double pntR = m_Radius0;
  double prevPntR = m_Radius0;
  double mness;
  unsigned int kernMid = m_NumKernelPoints / 2;

  KernArrayType pntKernArray;
  int step = 1;
  if( kernPntStart > kernPntEnd )
    {
    step = -1;
    }

  unsigned int count = 0;
  for( int kernPnt = (int)( kernPntStart);
    kernPnt != (int)( kernPntEnd)+step;
    kernPnt += step )
    {
    pntKernArray.clear();
    for( int j = (int)( kernPnt)-(int)( kernMid);
      j <= (int)( kernPnt)+(int)( kernMid); j++ )
      {
      if( ( j - kernPntStart ) * step > 0 &&
          ( kernPntEnd - j ) * step > 0 )
        {
        pntKernArray.push_back( kernArray[j] );
        }
      else
        {
        pntKernArray.push_back( kernArray[kernPnt] );
        }
      }

    static_cast< RadiusExtractorMedialnessFunc< TInputImage > *>(
      m_MedialnessFunc )->SetKernelArray( & pntKernArray );
    m_MedialnessOptSpline->newData( true );
    pntR /= m_MedialnessScaleStep;
    m_MedialnessOptSpline->extreme( &pntR, &mness );
    pntR *= m_MedialnessScaleStep;

    if( mness < m_ThreshMedialness )
      {
      if( m_Debug )
        {
        std::cout << "Bad mnessVal( " << pntR << " ) = " << mness
          << std::endl;
        }
      pntR = prevPntR;
      pntR /= m_MedialnessScaleStep;
      m_MedialnessOptSpline->extreme( &pntR, &mness );
      pntR *= m_MedialnessScaleStep;
      if( mness >= m_ThreshMedialness )
        {
        if( m_Debug )
          {
          std::cout << "   *** new mnessVal( " << pntR << " ) = " << mness
            << std::endl;
          }
        if( mness > 2 * m_ThreshMedialness )
          {
          prevPntR = pntR;
          }
        }
      else
        {
        pntR = prevPntR;
        mness = 2 * m_ThreshMedialness;
        if( m_Debug )
          {
          std::cout << "   using old mnessVal( " << pntR << " ) = "
            << mness << std::endl;
          }
        }
      }
    else
      {
      if( mness > 2 * m_ThreshMedialness )
        {
        prevPntR = pntR;
        }
      }

    for( int j = (int)( kernPnt)-(int)( kernMid);
      j <= (int)( kernPnt)+(int)( kernMid); j++ )
      {
      if( ( j - kernPntStart ) * step > 0 &&
          ( kernPntEnd - j ) * step > 0 )
        {
        if( kernArray[j].GetRadius() > 0 )
          {
          mness = 1.0 - vnl_math_abs( (double)( j - kernPnt ))
            / ( kernMid + 1.0 );
          mness = kernArray[j].GetRadius()
            + mness * ( pntR - kernArray[j].GetRadius() );
          kernArray[j].SetRadius( mness );
          }
        else
          {
          kernArray[j].SetRadius( pntR );
          }
        }
      }

    count++;
    if( count/5 == count/5.0 && m_StatusCallBack )
      {
      char mesg[80];
      sprintf( mesg, "Radius at %d = %f", count*m_KernelPointSpacing,
        pntR );
      char loc[80];
      sprintf( loc, "Extract:Widths" );
      m_StatusCallBack( loc, mesg, 0 );
      }
    }
}

/** Compute Kernel Measures */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::SmoothMeasuresInFullKernelArray( KernArrayType & kernArray )
{
  unsigned int len = kernArray.size();

  for( unsigned int iter=0; iter<20; iter++ )
    {
    for( unsigned int kernPnt=0; kernPnt<len-1; kernPnt++ )
      {
      kernArray[kernPnt].SetRadius(
        ( 1.0 * kernArray[kernPnt].GetRadius()
        + 1.0 * kernArray[kernPnt+1].GetRadius() ) / 2.0 );
      }
    for( int kernPnt=(int)( len)-1; kernPnt>0; kernPnt-- )
      {
      kernArray[kernPnt].SetRadius(
        ( 1.0 * kernArray[kernPnt].GetRadius()
        + 1.0 * kernArray[kernPnt-1].GetRadius() ) / 2.0 );
      }
    }

  unsigned int kernMid = ( m_NumKernelPoints - 1 ) / 2;

  KernArrayType kernTemp;
  for( unsigned int kernPnt=0; kernPnt<len; kernPnt++ )
    {
    kernTemp.clear();
    for( int j = (int)( kernPnt)-(int)( kernMid);
      j <= (int)( kernPnt)+(int)( kernMid); j++ )
      {
      if( j >= 0 && j < (int)( len) )
        {
        kernTemp.push_back( kernArray[j] );
        }
      else
        {
        kernTemp.push_back( kernArray[kernPnt] );
        }
      }

    double mness = 0;
    double bness = 0;
    this->ComputeMeasuresInKernelArray( kernTemp,
      kernArray[kernPnt].GetRadius(), mness, bness, true );
    kernArray[kernPnt].SetMedialness( mness );
    kernArray[kernPnt].SetBranchness( bness );
    }
}

/**
 * Apply kernel measures */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::ApplyMeasuresInFullKernelArray( TubeType & tube,
  KernArrayType & kernArray,
  KernArrayTubePointIndexType & kernArrayTubePointIndex )
{

  unsigned int len = kernArray.size();

  if( len == 0 )
    {
    return;
    }

  if( kernArrayTubePointIndex[0] >= len )
    {
    return;
    }

  double r0 = kernArray[0].GetRadius();
  double m0 = kernArray[0].GetMedialness();
  double b0 = kernArray[0].GetBranchness();

  KernArrayType & pnts = tube.GetPoints();
  typename KernArrayType::iterator pntIter = pnts.begin();

  for( unsigned int pntCnt = 0; pntCnt < kernArrayTubePointIndex[0];
    pntCnt++ )
    {
    (*pntIter).SetRadius( r0 );
    (*pntIter).SetMedialness( m0 );
    (*pntIter).SetBranchness( b0 );
    pntIter++;
    }

  double w, r, m, b;

  double r1 = r0;
  double m1 = m0;
  double b1 = b0;
  for( unsigned int arrayCnt=0; arrayCnt<len-1; arrayCnt++ )
    {
    r0 = r1;
    m0 = m1;
    b0 = b1;
    r1 = kernArray[arrayCnt+1].GetRadius();
    m1 = kernArray[arrayCnt+1].GetMedialness();
    b1 = kernArray[arrayCnt+1].GetBranchness();
    for( unsigned int pntCnt = 0;
      pntCnt < kernArrayTubePointIndex[arrayCnt+1]; pntCnt++ )
      {
      w = pntCnt / (double)m_KernelPointSpacing;
      r = r0 + w * ( r1 - r0 );
      m = m0 + w * ( m1 - m0 );
      b = b0 + w * ( b1 - b0 );
      (*pntIter).SetRadius( r );
      (*pntIter).SetMedialness( m );
      (*pntIter).SetBranchness( b );
      pntIter++;
      }
    }

  r0 = kernArray[len-1].GetRadius();
  m0 = kernArray[len-1].GetMedialness();
  b0 = kernArray[len-1].GetBranchness();
  while( pntIter != tube.GetPoints().end() )
    {
    (*pntIter).SetRadius( r0 );
    (*pntIter).SetMedialness( m0 );
    (*pntIter).SetBranchness( b0 );
    pntIter++;
    }

  if( m_StatusCallBack )
    {
    char mesg[80];
    sprintf( mesg, "Applied to %ld tube points.",
      tube.GetPoints().size() );
    char loc[80];
    sprintf( loc, "Extract:Widths" );
    m_StatusCallBack( loc, mesg, 0 );
    }
  else
    {
    std::cout << "Applied to " << tube.GetPoints().size() << "tube points."
       << std::endl;
    }
}

/** Compute Radii */
template<class TInputImage>
bool
RadiusExtractor<TInputImage>
::ComputeTubeRadii( TubeType & tube )
{
  if( tube.GetPoints().size() == 0 )
    {
    return false;
    }

  KernArrayType               kernArray;
  KernArrayTubePointIndexType kernArrayTubePointIndex;
  this->ComputeValuesInFullKernelArray( tube, kernArray,
    kernArrayTubePointIndex );

  typename KernArrayType::iterator pntIter;
  pntIter = tube.GetPoints().begin();
  while( (*pntIter).GetID() != 0 && pntIter != tube.GetPoints().end() )
    {
    pntIter++;
    }

  if( m_Debug )
    {
    std::cout << "Found point " << ( *pntIter ).GetID() << std::endl;
    }

  unsigned int len = kernArray.size();

  int minDistI = 0;
  double minDist = ComputeEuclideanDistance( kernArray[0].GetPosition(),
    (*pntIter).GetPosition() );
  for( unsigned int kPnt=1; kPnt<len; kPnt++ )
    {
    double tf = ComputeEuclideanDistance( kernArray[kPnt].GetPosition(),
      (*pntIter).GetPosition() );
    if( tf < minDist )
      {
      minDist = tf;
      minDistI = (int)( kPnt);
      }
    }

  if( m_Debug )
    {
    std::cout << "Found point i = " << minDistI << std::endl;
    }

  int kernPnt = minDistI - 1;
  if( kernPnt < 0 )
    {
    kernPnt = 0;
    }

  this->ComputeMeasuresInFullKernelArray( kernArray, kernPnt, len-1 );

  kernPnt = minDistI + 1;
  if( kernPnt > (int)( len)-1 )
    {
    kernPnt = (int)( len) - 1;
    }

  this->ComputeMeasuresInFullKernelArray( kernArray, kernPnt, 0 );

  this->SmoothMeasuresInFullKernelArray( kernArray );

  this->ApplyMeasuresInFullKernelArray( tube, kernArray,
    kernArrayTubePointIndex );

  return true;
}

/**
 * Idle callback */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::SetIdleCallBack( bool ( *idleCallBack )() )
{
  m_IdleCallBack = idleCallBack;
}

/**
 * Status Call back */
template<class TInputImage>
void
RadiusExtractor<TInputImage>
::SetStatusCallBack( void ( *statusCallBack )( char *, char *, int ) )
{
  m_StatusCallBack = statusCallBack;
}


}; // end namespace itk

#endif
