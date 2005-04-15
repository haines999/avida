
#include "py_avida_driver.hh"

#include "cpu/hardware_base.hh"
#include "cpu/hardware_factory.hh"
#include "main/config.hh"
#include "main/genebank.hh"
#include "main/genotype.hh"
#include "main/organism.hh"
#include "main/population.hh"
#include "main/population_cell.hh"
#include "tools/change_list.hh"
#include "tools/string.hh"


#include <iostream>


bool pyAvidaDriver::preUpdate(const unsigned int){
  if (cChangeList *change_list = population->GetChangeList()) {
    change_list->Reset();
  }
  GetEvents();
  if(true == done_flag){ return false; }
  // Increment the Update.
  cStats &stats = population->GetStats();
  stats.IncCurrentUpdate();
  // Handle all data collection for previous update.
  if(stats.GetUpdate() > 0){
    // Tell the stats object to do update calculations and printing.
    stats.ProcessUpdate();
    // Update all the genotypes for the end of this update.
    cGenebank &genebank = population->GetGenebank();
    for(
      cGenotype *cur_genotype = genebank.ResetThread(0);
      cur_genotype != 0 && cur_genotype->GetThreshold();
      cur_genotype = genebank.NextGenotype(0)
    ){
      cur_genotype->UpdateReset();
    }
  }

  // Prepare to start next update.
  m_update_size = cConfig::GetAveTimeslice() * population->GetNumOrganisms();
  m_step_size = 1. / m_update_size;
  m_update_progress = 0;
  // Enter next stage of update.
  m_update_stage_function = m_update_mode_function;
  return true;
}
bool pyAvidaDriver::fastUpdate(const unsigned int bite_size){
  for(unsigned int i = 0; i < bite_size; i++){
    if(m_update_progress < m_update_size){
      m_current_cell_id = population->ScheduleOrganism();
      population->ProcessStep(m_step_size, m_current_cell_id);
      m_update_progress++;
    } else {
      // Enter next stage of update.
      m_update_stage_function = &pyAvidaDriver::postUpdate;
      return true; // Early exit from for loop.
    }
  }
  return true;
}
bool pyAvidaDriver::stepUpdate(const unsigned int bite_size){
  for(unsigned int i = 0; i < bite_size; i++){
    if(m_update_progress < m_update_size){
      m_update_progress++;
      m_current_cell_id = population->ScheduleOrganism();
      population->ProcessStep(m_step_size, m_current_cell_id);
      if(m_current_cell_id == m_step_cell_id){ return false; } // Early exit from for loop.
    } else {
      // Enter next stage of update.
      m_update_stage_function = &pyAvidaDriver::postUpdate;
      return true; // Early exit from for loop.
    }
  }
  return true;
}
bool pyAvidaDriver::postUpdate(const unsigned int){
  // End-of-update stats...
  population->CalcUpdateStats();
  // Print out status for this update.
  //cStats &stats = population->GetStats();
  //std::cout
  //<< "UD: " << stats.GetUpdate() << "\t"
  //<< "Gen: " << stats.SumGeneration().Average() << "\t"
  //<< "Fit: " << stats.GetAveFitness() << "\t"
  //<< "Size: " << population->GetNumOrganisms() << "\t"
  //<< std::endl;
  // Check whether to do point mutations.
  if(cConfig::GetPointMutProb() > 0.){
    m_mutations_progress = 0;
    // Enter next stage of update.
    m_update_stage_function = &pyAvidaDriver::ptMutations;
  } else {
    // Enter next stage of update.
    m_update_stage_function = &pyAvidaDriver::postPtMutations;
  }
  return true;
}
bool pyAvidaDriver::ptMutations(const unsigned int){
  if(m_mutations_progress < population->GetSize()){
    cPopulationCell &cell = population->GetCell(m_mutations_progress);
    if(cell.IsOccupied())
    cell.GetOrganism()->GetHardware().PointMutate(cConfig::GetPointMutProb());
    m_mutations_progress++;
  } else {
    // Enter next stage of update.
    m_update_stage_function = &pyAvidaDriver::postPtMutations;
  }
  return true;
}
bool pyAvidaDriver::postPtMutations(const unsigned int){
  // Do any cleanup in the hardware factory...
  cHardwareFactory::Update();
  // Exit conditions...
  if(0 == population->GetNumOrganisms()){
    done_flag = true;
  }
  // Prepare for first stage of new update.
  m_update_stage_function = &pyAvidaDriver::preUpdate;
  return false;
}

cChangeList *pyAvidaDriver::GetChangeList(){
  return population->GetChangeList();
}

pyAvidaDriver::pyAvidaDriver(cEnvironment & environment)
: cAvidaDriver_Population(environment)
, m_update_mode_function(&pyAvidaDriver::fastUpdate)
, m_update_stage_function(&pyAvidaDriver::preUpdate)
, m_step_cell_id(-1)
, m_change_list(new cChangeList())
{
  population->SetChangeList(m_change_list);
}

pyAvidaDriver::~pyAvidaDriver(){
  delete m_change_list;
}
