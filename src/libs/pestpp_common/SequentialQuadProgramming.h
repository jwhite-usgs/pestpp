#ifndef SEQUENTIALQUADPROGRAMMING_H_
#define SEQUENTIALQUADPROGRAMMING_H_

#include <map>
#include <random>
#include <mutex>
#include <thread>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include "FileManager.h"
#include "ObjectiveFunc.h"
#include "OutputFileWriter.h"
#include "PerformanceLog.h"
#include "RunStorage.h"
#include "covariance.h"
#include "RunManagerAbstract.h"
#include "ObjectiveFunc.h"
#include "Localizer.h"
#include "EnsembleMethodUtils.h"
#include "constraints.h"







class SeqQuadProg
{
public:
	SeqQuadProg(Pest& _pest_scenario, FileManager& _file_manager,
		OutputFileWriter& _output_file_writer, PerformanceLog* _performance_log,
		RunManagerAbstract* _run_mgr_ptr);
	
	void initialize();
	void iterate_2_solution();
	void finalize();
	void throw_sqp_error(string message);

private:
	int  verbose_level;
	Pest &pest_scenario;
	FileManager &file_manager;
	std::mt19937 rand_gen;
	std::mt19937 subset_rand_gen;
	OutputFileWriter &output_file_writer;
	PerformanceLog *performance_log;
	RunManagerAbstract* run_mgr_ptr;
	Covariance parcov, obscov;

	bool use_localizer;
	Localizer localizer;

	int num_threads;

	set<string> pp_args;

	int iter,subset_size;
	bool use_subset;

	vector<string> act_obs_names, act_par_names;
	vector<int> subset_idxs;

	
	
	template<typename T, typename A>
	void message(int level, const string &_message, vector<T, A> _extras, bool echo=true);
	void message(int level, const string &_message);

	template<typename T>
	void message(int level, const string &_message, T extra);

	void sanity_checks();

	
	void set_subset_idx(int size);

};

#endif
