/*======================================================================

  This file is part of the elastix software.

  Copyright (c) University Medical Center Utrecht. All rights reserved.
  See src/CopyrightElastix.txt or http://elastix.isi.uu.nl/legal.php for
  details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE. See the above copyright notices for more information.

======================================================================*/

#ifndef _itkGradientDescentOptimizer2_txx
#define _itkGradientDescentOptimizer2_txx

#include "itkGradientDescentOptimizer2.h"
#include "itkCommand.h"
#include "itkEventObject.h"
#include "itkExceptionObject.h"

#include "elxTimer.h" // tmp
#include <omp.h> // OpenMP
#include <Eigen/Dense> // Eigen
#include <Eigen/Core> // Eigen

namespace itk
{

/**
 * ****************** Constructor ************************
 */

  GradientDescentOptimizer2
    ::GradientDescentOptimizer2()
  {
    itkDebugMacro("Constructor");

    this->m_LearningRate = 1.0;
    this->m_NumberOfIterations = 100;
    this->m_CurrentIteration = 0;
    this->m_Value = 0.0;
    this->m_StopCondition = MaximumNumberOfIterations;

    this->m_NumberOfThreads = 1;
    this->m_UseMultiThread = false;
    this->m_UseOpenMP  = false;
    this->m_UseEigen = false;

  } // end Constructor


  /**
   * *************** PrintSelf *************************
   */

  void
    GradientDescentOptimizer2
    ::PrintSelf(std::ostream& os, Indent indent) const
  {
    this->Superclass::PrintSelf(os,indent);

    os << indent << "LearningRate: "
      << this->m_LearningRate << std::endl;
    os << indent << "NumberOfIterations: "
      << this->m_NumberOfIterations << std::endl;
    os << indent << "CurrentIteration: "
      << this->m_CurrentIteration;
    os << indent << "Value: "
      << this->m_Value;
    os << indent << "StopCondition: "
      << this->m_StopCondition;
    os << std::endl;
    os << indent << "Gradient: "
      << this->m_Gradient;
    os << std::endl;

  } // end PrintSelf


  /**
  * **************** Start the optimization ********************
  */

  void
    GradientDescentOptimizer2
    ::StartOptimization( void )
  {
    itkDebugMacro("StartOptimization");

    this->m_CurrentIteration   = 0;

    /** Get the number of parameters; checks also if a cost function has been set at all.
    * if not: an exception is thrown */
    this->GetScaledCostFunction()->GetNumberOfParameters();

    /** Initialize the scaledCostFunction with the currently set scales */
    this->InitializeScales();

    /** Set the current position as the scaled initial position */
    this->SetCurrentPosition( this->GetInitialPosition() );

    this->ResumeOptimization();
  } // end StartOptimization


  /**
  * ************************ ResumeOptimization *************
  */

  void
    GradientDescentOptimizer2
    ::ResumeOptimization( void )
  {
    itkDebugMacro("ResumeOptimization");

    this->m_Stop = false;

    InvokeEvent( StartEvent() );

    const unsigned int spaceDimension
      = this->GetScaledCostFunction()->GetNumberOfParameters();
    this->m_Gradient = DerivativeType( spaceDimension );

    while( ! this->m_Stop )
    {

      try
      {
        this->GetScaledValueAndDerivative(
          this->GetScaledCurrentPosition(), m_Value, m_Gradient );
      }
      catch ( ExceptionObject& err )
      {
        this->MetricErrorResponse( err );
      }

      /** StopOptimization may have been called. */
      if ( this->m_Stop )
      {
        break;
      }

      // tmp measure time
      typedef tmr::Timer          TimerType;
      typedef TimerType::Pointer  TimerPointer;
      TimerPointer timer = TimerType::New();
      timer->StartTimer();
      this->AdvanceOneStep();
      timer->StopTimer();
      /*elxout << "AdvanceOneStep() took: "
        << static_cast<long>( timer->GetElapsedClockSec() * 1000 )
        << " ms." << std::endl;*/
      this->m_AdvanceOneStepTimings.push_back( timer->GetElapsedClockSec() * 1000 );

      /** StopOptimization may have been called. */
      if ( this->m_Stop )
      {
        break;
      }

      this->m_CurrentIteration++;

      if ( m_CurrentIteration >= m_NumberOfIterations )
      {
        this->m_StopCondition = MaximumNumberOfIterations;
        this->StopOptimization();
        break;
      }

    } // end while

  } // end ResumeOptimization()


 /**
  * ***************** MetricErrorResponse ************************
  */

  void
    GradientDescentOptimizer2
    ::MetricErrorResponse( ExceptionObject & err )
  {
    /** An exception has occurred. Terminate immediately. */
    this->m_StopCondition = MetricError;
    this->StopOptimization();

    /** Pass exception to caller. */
    throw err;

  } // end MetricErrorResponse()


  /**
  * ***************** Stop optimization ************************
  */

  void
    GradientDescentOptimizer2
    ::StopOptimization( void )
  {
    itkDebugMacro("StopOptimization");

    this->m_Stop = true;
    this->InvokeEvent( EndEvent() );
  } // end StopOptimization


 /**
  * ************ AdvanceOneStep ****************************
  * following the gradient direction
  */

  void
    GradientDescentOptimizer2
    ::AdvanceOneStep( void )
  {
    itkDebugMacro("AdvanceOneStep");

    /** Get space dimension and allocate new position. */
    const unsigned int spaceDimension
      = this->GetScaledCostFunction()->GetNumberOfParameters();
    const double learningRate = this->m_LearningRate;
    // This old way creates a vector, fills it later, and copies it at the end
    //ParametersType newPosition( spaceDimension );
    // This new way gets a reference to a previously allocated vector and fills it
    // This saves and allocation and a copy at every iteration
    ParametersType & newPosition = this->m_ScaledCurrentPosition;

    if ( !this->m_UseMultiThread )
    {
      /** Get a reference to the current position. */
      const ParametersType & currentPosition = this->GetScaledCurrentPosition();

      /** Update the new position. */
      for( unsigned int j = 0; j < spaceDimension; j++ )
      {
        newPosition[j] = currentPosition[j] - this->m_LearningRate * this->m_Gradient[j];
      }
    }
    else if ( this->m_UseOpenMP && !this->m_UseEigen )
    {
      /** Get a reference to the current position. */
      const ParametersType & currentPosition = this->GetScaledCurrentPosition();

      /** Update the new position. */
      #pragma omp parallel for
      for( int j = 0; j < spaceDimension; j++ )
      {
        newPosition[j] = currentPosition[j] - this->m_LearningRate * this->m_Gradient[j];
      }
    }
    else if ( !this->m_UseOpenMP && this->m_UseEigen )
    {
      /** Get a reference to the current position. */
      const ParametersType & currentPosition = this->GetScaledCurrentPosition();

      /** Wrap itk::Arrays into Eigen jackets. */
      typedef Eigen::VectorXd ParametersTypeEigen;
      Eigen::Map<ParametersTypeEigen> newPositionE( newPosition.data_block(), spaceDimension );
      Eigen::Map<const ParametersTypeEigen> currentPositionE( currentPosition.data_block(), spaceDimension );
      Eigen::Map<ParametersTypeEigen> gradientE( this->m_Gradient.data_block(), spaceDimension );

      /** Update the new position. */
      newPositionE = currentPositionE - learningRate * gradientE;
    }
    else if ( this->m_UseOpenMP && this->m_UseEigen )
    {
      /** Get a reference to the current position. */
      const ParametersType & currentPosition = this->GetScaledCurrentPosition();

      /** Wrap itk::Arrays into Eigen jackets. */
      typedef Eigen::VectorXd ParametersTypeEigen;
      Eigen::Map<ParametersTypeEigen> newPositionE( newPosition.data_block(), spaceDimension );
      Eigen::Map<const ParametersTypeEigen> currentPositionE( currentPosition.data_block(), spaceDimension );
      Eigen::Map<ParametersTypeEigen> gradientE( this->m_Gradient.data_block(), spaceDimension );

      /** Update the new position. */
      const int nthreads = this->m_NumberOfThreads;
      omp_set_num_threads( nthreads );
      #pragma omp parallel for
      for( int i = 0; i < nthreads; i += 1 )
      {
        int threadId = omp_get_thread_num();
        int chunk = ( spaceDimension + nthreads - 1 ) / nthreads;
        int jmin = threadId * chunk;
        int jmax = (threadId + 1) * chunk < spaceDimension ? (threadId + 1) * chunk : spaceDimension;
        int subSize = jmax - jmin;

        newPositionE.segment( jmin, subSize ) = currentPositionE.segment( jmin, subSize )
          - learningRate * gradientE.segment( jmin, subSize );
      }
    }
    else
    {
      /** Fill the threader parameter struct with information. */
      MultiThreaderParameterType * temp = new  MultiThreaderParameterType;
      temp->t_NewPosition = &newPosition;
      temp->t_Optimizer = this;

      /** Call multi-threaded AdvanceOneStep(). */
      ThreaderType::Pointer local_threader = ThreaderType::New();
      local_threader->SetNumberOfThreads( this->m_NumberOfThreads );
      local_threader->SetSingleMethod( AdvanceOneStepThreaderCallback, (void *)( temp ) );
      local_threader->SingleMethodExecute();

      delete temp;
    }

    //this->SetScaledCurrentPosition( newPosition ); // has explicit copy

    this->InvokeEvent( IterationEvent() );

  } // end AdvanceOneStep()


/**
 * ************ AdvanceOneStepThreaderCallback ****************************
 */

ITK_THREAD_RETURN_TYPE GradientDescentOptimizer2
::AdvanceOneStepThreaderCallback( void * arg )
{
  /** Get the current thread id and user data. */
  ThreadInfoType * infoStruct = static_cast< ThreadInfoType * >( arg );
  ThreadIdType threadID = infoStruct->ThreadID;
  MultiThreaderParameterType * temp
    = static_cast<MultiThreaderParameterType * >( infoStruct->UserData );

  /** Call the real implementation. */
  temp->t_Optimizer->ThreadedAdvanceOneStep( threadID, *(temp->t_NewPosition) );

  return ITK_THREAD_RETURN_VALUE;

} // end AdvanceOneStepThreaderCallback()


/**
 * ************ ThreadedAdvanceOneStep ****************************
 */

void GradientDescentOptimizer2
::ThreadedAdvanceOneStep( ThreadIdType threadId, ParametersType & newPosition )
{
  /** Compute the range for this thread. */
  const unsigned int spaceDimension
    = this->GetScaledCostFunction()->GetNumberOfParameters();
  const unsigned int subSize = static_cast<unsigned int>(
    vcl_ceil( static_cast<double>( spaceDimension )
    / static_cast<double>( this->m_NumberOfThreads ) ) );
  const unsigned int jmin = threadId * subSize;
  unsigned int jmax = ( threadId + 1 ) * subSize;
  jmax = ( jmax > spaceDimension ) ? spaceDimension : jmax ;

  /** Get a reference to the current position. */
  const ParametersType & currentPosition = this->GetScaledCurrentPosition();
  const double learningRate = this->m_LearningRate;
  const DerivativeType & gradient = this->m_Gradient;

  /** Advance one step: mu_{k+1} = mu_k - a_k * gradient_k */
  for( unsigned int j = jmin; j < jmax; j++ )
  {
    //newPosition[ j ] = currentPosition[ j ] - this->m_LearningRate * this->m_Gradient[ j ];
    newPosition[ j ] = currentPosition[ j ] - learningRate * gradient[ j ];
  }

} // end ThreadedAdvanceOneStep()


} // end namespace itk

#endif
