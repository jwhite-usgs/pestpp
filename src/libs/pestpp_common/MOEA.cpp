#include <random>
#include <iomanip>
#include <iterator>
#include <map>
#include "MOEA.h"
#include "Ensemble.h"
#include "RunManagerAbstract.h"
#include "ModelRunPP.h"
#include "RestartController.h"
#include "EnsembleMethodUtils.h"
#include "constraints.h"

using namespace std;

ParetoObjectives::ParetoObjectives(Pest& _pest_scenario, FileManager& _file_manager, PerformanceLog* _performance_log)
	: pest_scenario(_pest_scenario),file_manager(_file_manager),performance_log(_performance_log)

{

}

pair<vector<string>,vector<string>> ParetoObjectives::pareto_dominance_sort(const vector<string>& obj_names, ObservationEnsemble& op, ParameterEnsemble& dp, map<string,double>& obj_dir_mult)
{
	stringstream ss;
	ss << "ParetoObjectives::pareto_dominance_sort() for " << op.shape().first << " population members";
	performance_log->log_event(ss.str());
	performance_log->log_event("preparing fast-lookup containers");
	//TODO:add additional arg for prior info objectives and augment the below storage containers with PI-based objective values
	//while accounting for obj dir mult
	
	//TODO: check for a single objective and deal appropriately

	//first prep two fast look up containers, one by obj and one by member name
	map<string, map<double,string>> obj_struct;
	vector<string> real_names = op.get_real_names();
	Eigen::VectorXd obj_vals;
	map<string, map<string, double>> temp;
	for (auto obj_name : obj_names)
	{
		obj_vals = op.get_eigen(vector<string>(), vector<string>{obj_name});

		//if this is a max obj, just flip the values here
		if (obj_dir_mult.find(obj_name) != obj_dir_mult.end())
		{
			obj_vals *= obj_dir_mult[obj_name];
		}
		map<double,string> obj_map;
		map<string, double> t;
		for (int i = 0; i < real_names.size(); i++)
		{
			obj_map[obj_vals[i]] = real_names[i];
			t[real_names[i]] = obj_vals[i];
		}
		obj_struct[obj_name] = obj_map;
		temp[obj_name] = t;

	}

	map<string, map<string,double>> member_struct;
	for (auto real_name : real_names)
	{
		map<string,double> obj_map;
		for (auto t : temp)
		{
			//obj_map[t.second[real_name]] = t.first;
			obj_map[t.first] = t.second[real_name];
		}
			
		member_struct[real_name] = obj_map;
	}
	temp.clear();
	
	map<int, vector<string>> front_map = sort_members_by_dominance_into_fronts(member_struct);
	performance_log->log_event("sorting done");
	ofstream& frec = file_manager.rec_ofstream();
	
	frec << "...pareto dominance sort yielded " << front_map.size() << " domination fronts" << endl;
	for (auto front : front_map)
	{
		frec << front.second.size() << " in the front " << front.first << endl;
	}

	
	
	vector<string> nondom_crowd_ordered,dom_crowd_ordered;
	vector<string> crowd_ordered_front;
	for (auto front : front_map)
	{	//TODO: Deb says we only need to worry about crowding sort if not all
		//members of the front are going to be retained.  For now, just sorting all fronts...
		if (front.second.size() == 1)
			crowd_ordered_front = front.second;
		else
			crowd_ordered_front = sort_members_by_crowding_distance(front.second, member_struct);
		if (front.first == 1)
			for (auto front_member : crowd_ordered_front)
				nondom_crowd_ordered.push_back(front_member);
		else
			for (auto front_member : crowd_ordered_front)
				dom_crowd_ordered.push_back(front_member);
	}
	if (op.shape().first != nondom_crowd_ordered.size() + dom_crowd_ordered.size())
	{
		ss.str("");
		ss << "ParetoObjectives::pareto_dominance_sort() internal error: final sorted population size: " << 
			nondom_crowd_ordered.size() + dom_crowd_ordered.size() << " != initial population size: " << op.shape().first;
		cout << ss.str();
		throw runtime_error(ss.str());
	}

	//TODO: some kind of reporting here.  Probably to the rec and maybe a csv file too...

	return pair<vector<string>, vector<string>>(nondom_crowd_ordered, dom_crowd_ordered);

}

vector<string> ParetoObjectives::sort_members_by_crowding_distance(vector<string>& members, map<string,map<string,double>>& memeber_struct)
{

	typedef std::function<bool(std::pair<std::string, double>, std::pair<std::string, double>)> Comparator;
	// Defining a lambda function to compare two pairs. It will compare two pairs using second field
	Comparator compFunctor = [](std::pair<std::string, double> elem1, std::pair<std::string, double> elem2)
	{
		return elem1.second < elem2.second;
	};
	
	map<string, map<string,double>> obj_member_map;
	map<string, double> crowd_distance_map;
	string m = members[0];
	vector<string> obj_names;
	for (auto obj_map : memeber_struct[m])
	{
		obj_member_map[obj_map.first] = map<string,double>();
		obj_names.push_back(obj_map.first);
	}

	for (auto member : members)
	{
		crowd_distance_map[member] = 0.0;
		for (auto obj_map : memeber_struct[member])
			obj_member_map[obj_map.first][member] = obj_map.second;

	}

	//map<double,string>::iterator start, end;
	map<string,double> omap;
	double obj_range;
	typedef std::set<std::pair<std::string, double>, Comparator> crowdset;
	for (auto obj_map : obj_member_map)
	{
		omap = obj_map.second;
		crowdset crowd_sorted(omap.begin(),omap.end(), compFunctor);

		crowdset::iterator start = crowd_sorted.begin(), last = prev(crowd_sorted.end(), 1);


		obj_range = last->second - start->second;

		//the obj extrema - makes sure they are retained 
		crowd_distance_map[start->first] = 1.0e+30;
		crowd_distance_map[last->first] = 1.0e+30;
		if (members.size() > 2)
		{
			//need iterators to start and stop one off from the edges
			start = next(crowd_sorted.begin(), 1);
			last = prev(crowd_sorted.end(), 2);

			crowdset::iterator it = start;

			crowdset::iterator inext, iprev;
			for (; it != last; ++it)
			{
				iprev = prev(it, 1);
				inext = next(it, 1);
				crowd_distance_map[it->first] = crowd_distance_map[it->first] + ((inext->second - iprev->second) / obj_range);
			}
		}

	}

	vector <pair<string, double>> cs_vec;
	for (auto cd : crowd_distance_map)
		cs_vec.push_back(cd);

	std::sort(cs_vec.begin(), cs_vec.end(),
		compFunctor);

	vector<string> crowd_ordered;
	for (auto cs : cs_vec)
		crowd_ordered.push_back(cs.first);

	//TODO: check here that all solutions made it thru the crowd distance sorting
	if (crowd_ordered.size() != members.size())
		throw runtime_error("ParetoObjectives::sort_members_by_crowding_distance() error: final sort size != initial size");
	return crowd_ordered;
}

map<int,vector<string>> ParetoObjectives::sort_members_by_dominance_into_fronts(map<string, map<string, double>>& member_struct)
{
	//following fast non-dom alg in Deb
	performance_log->log_event("starting 'fast non-dom sort");
	//map<string,map<string,double>> Sp, F1;
	map<string, vector<string>> solutions_dominated_map;
	int domination_counter;
	map<string, int> num_dominating_map;
	vector<string> solutions_dominated, first_front;
	performance_log->log_event("finding first front");
	for (auto solution_p : member_struct)
	{
		domination_counter = 0;
		solutions_dominated.clear();
		for (auto solution_q : member_struct)
		{
			if (solution_p.first == solution_q.first) //string compare real name
				continue;
			if (first_dominates_second(solution_p.second, solution_q.second))
			{
				solutions_dominated.push_back(solution_q.first);
			}
			else if (first_dominates_second(solution_q.second, solution_p.second))
			{
				domination_counter++;
			}
		}
		//solution_p is in the first front
		if (domination_counter == 0)
		{
			first_front.push_back(solution_p.first);
		}
		num_dominating_map[solution_p.first] = domination_counter;
		solutions_dominated_map[solution_p.first] = solutions_dominated;

	}
	performance_log->log_event("sorting remaining fronts");
	int i = 1;
	int nq;
    vector<string> q_front;
	map<int, vector<string>> front_map;
	front_map[1] = first_front;
	vector<string> front = first_front;
	while (true)
	{
		
		q_front.clear();
		for (auto solution_p : front)
		{
			solutions_dominated = solutions_dominated_map[solution_p];
			for (auto solution_q : solutions_dominated)
			{
				num_dominating_map[solution_q]--;
				if (num_dominating_map[solution_q] <= 0)
					q_front.push_back(solution_q);
			}
		}
		if (q_front.size() == 0)
			break;
		i++;
		front_map[i] = q_front;
		
		front = q_front;
	}
	//TODO: check that all solutions made it thru the sorting process
	return front_map;
}

bool ParetoObjectives::first_dominates_second(map<string,double>& first, map<string,double>& second)
{
	for (auto f: first)
	{
		if (f.second > second[f.first])
			return false;
	}
	return true;
}

MOEA::MOEA(Pest &_pest_scenario, FileManager &_file_manager, OutputFileWriter &_output_file_writer, 
	PerformanceLog *_performance_log, RunManagerAbstract* _run_mgr_ptr)
	: pest_scenario(_pest_scenario), file_manager(_file_manager),
	output_file_writer(_output_file_writer), performance_log(_performance_log),
	run_mgr_ptr(_run_mgr_ptr), constraints(_pest_scenario, &_file_manager, _output_file_writer, *_performance_log),
	objectives(_pest_scenario,_file_manager,_performance_log)
	
{
	rand_gen = std::mt19937(pest_scenario.get_pestpp_options().get_random_seed());
	dp.set_rand_gen(&rand_gen);
	dp.set_pest_scenario(&pest_scenario);
	op.set_rand_gen(&rand_gen);
	op.set_pest_scenario(&pest_scenario);
	dp_archive.set_rand_gen(&rand_gen);
	dp_archive.set_pest_scenario(&pest_scenario);
	op_archive.set_rand_gen(&rand_gen);
	op_archive.set_pest_scenario(&pest_scenario);
}


template<typename T, typename A>
void MOEA::message(int level, const string& _message, vector<T, A> _extras, bool echo)
{
	stringstream ss;
	if (level == 0)
		ss << endl << "  ---  ";
	else if (level == 1)
		ss << "...";
	ss << _message;
	if (_extras.size() > 0)
	{

		for (auto& e : _extras)
			ss << e << " , ";

	}
	if (level == 0)
		ss << "  ---  ";
	if (echo)// && ((verbose_level >= 2) || (level < 2)))
		cout << ss.str() << endl;
	file_manager.rec_ofstream() << ss.str() << endl;
	performance_log->log_event(_message);

}

void MOEA::message(int level, const string& _message)
{
	message(level, _message, vector<string>());
}

template<typename T>
void MOEA::message(int level, const string& _message, T extra)
{
	stringstream ss;
	ss << _message << " " << extra;
	string s = ss.str();
	message(level, s);
}

void MOEA::throw_moea_error(const string& message)
{
	performance_log->log_event("MOEA error: " + message);
	cout << endl << "   ************   " << endl << "    MOEAerror: " << message << endl << endl;
	file_manager.rec_ofstream() << endl << "   ************   " << endl << "    MOEA error: " << message << endl << endl;
	file_manager.close_file("rec");
	performance_log->~PerformanceLog();
	throw runtime_error("MOEA error: " + message);
}

void MOEA::sanity_checks()
{
	PestppOptions* ppo = pest_scenario.get_pestpp_options_ptr();
	vector<string> errors;
	vector<string> warnings;
	stringstream ss;

	if ((population_dv_file.size() == 0) && (population_obs_restart_file.size() > 0))
		errors.push_back("population_dv_file is empty but population_obs_restart_file is not - how can this work?");
	
	if ((ppo->get_mou_population_size() < error_min_members) && (population_dv_file.size() == 0))
	{
		ss.str("");
		ss << "population_size < " << error_min_members << ", this is redic, increaing to " << warn_min_members;
		warnings.push_back(ss.str());
		ppo->set_ies_num_reals(warn_min_members);
	}
	if ((ppo->get_mou_population_size() < warn_min_members) && (population_dv_file.size() == 0))
	{
		ss.str("");
		ss << "mou_population_size < " << warn_min_members << ", this is prob too few";
		warnings.push_back(ss.str());
	}
	if (ppo->get_ies_reg_factor() < 0.0)
		errors.push_back("ies_reg_factor < 0.0 - WRONG!");
	
	
	if (warnings.size() > 0)
	{
		message(0, "sanity_check warnings");
		for (auto& w : warnings)
			message(1, w);
		message(1, "continuing initialization...");
	}
	if (errors.size() > 0)
	{
		message(0, "sanity_check errors - uh oh");
		for (auto& e : errors)
			message(1, e);
		throw_moea_error(string("sanity_check() found some problems - please review rec file"));
	}
	//cout << endl << endl;
}


void MOEA::update_archive(ObservationEnsemble& _op, ParameterEnsemble& _dp)
{
	message(2, "updating archive");
	stringstream ss;
	if (op_archive.shape().first != dp_archive.shape().first)
	{
		ss.str("");
		ss << "MOEA::update_archive(): op_archive members " << op_archive.shape().first << " != dp_archive members " << dp_archive.shape().first;
		throw_moea_error(ss.str());
	}

	//check that solutions in nondominated solution in dompair.first are not in the archive already
	vector<string> keep, temp = op.get_real_names();
	set<string> archive_members(temp.begin(), temp.end());
	for (auto& member : _op.get_real_names())
	{
		if (archive_members.find(member) != archive_members.end())
			keep.push_back(member);
	}
	if (keep.size() == 0)
	{
		message(2, "all nondominated members in already in archive");
		return;
	}
	
	ss.str("");
	ss << "adding " << keep.size() << " non-dominated members to archive";
	message(2, ss.str());
	Eigen::MatrixXd other = _op.get_eigen(keep, vector<string>());
	op_archive.append_other_rows(keep, other);
	other = _dp.get_eigen(keep, vector<string>());
	dp_archive.append_other_rows(keep, other);
	other.resize(0, 0);
	performance_log->log_event("archive pareto sort");
	DomPair dompair = objectives.pareto_dominance_sort(obj_names, op_archive, dp_archive, obj_dir_mult);
	
	ss.str("");
	ss << "resizing archive from " << op_archive.shape().first << " to " << dompair.first.size() << " current non-dominated solutions";
	message(2, ss.str());
	op_archive.keep_rows(dompair.first);
	dp_archive.keep_rows(dompair.first);

	if (op_archive.shape().first > archive_size)
	{
		ss.str("");
		ss << "trimming archive size from " << op_archive.shape().first << " to max archive size " << archive_size;
		message(2, ss.str());
		vector<string> members = op_archive.get_real_names();
		keep.clear();
		for (int i = 0; i < archive_size; i++)
			keep.push_back(members[i]);
		op_archive.keep_rows(keep);
		dp_archive.keep_rows(keep);
	}
	
}


vector<int> MOEA::run_population(ParameterEnsemble& _pe, ObservationEnsemble& _oe, const vector<int>& real_idxs)
{
	message(1, "running population of size ", _pe.shape().first);
	stringstream ss;
	ss << "queuing " << _pe.shape().first << " runs";
	performance_log->log_event(ss.str());
	run_mgr_ptr->reinitialize();
	map<int, int> real_run_ids;
	try
	{
		real_run_ids = _pe.add_runs(run_mgr_ptr, real_idxs);
	}
	catch (const exception& e)
	{
		stringstream ss;
		ss << "run_ensemble() error queueing runs: " << e.what();
		throw_moea_error(ss.str());
	}
	catch (...)
	{
		throw_moea_error(string("run_ensemble() error queueing runs"));
	}
	performance_log->log_event("making runs");
	try
	{
		run_mgr_ptr->run();
	}
	catch (const exception& e)
	{
		stringstream ss;
		ss << "error running ensemble: " << e.what();
		throw_moea_error(ss.str());
	}
	catch (...)
	{
		throw_moea_error(string("error running ensemble"));
	}

	performance_log->log_event("processing runs");
	if (real_idxs.size() > 0)
	{
		_oe.keep_rows(real_idxs);
	}
	vector<int> failed_real_indices;
	try
	{
		failed_real_indices = _oe.update_from_runs(real_run_ids, run_mgr_ptr);
	}
	catch (const exception& e)
	{
		stringstream ss;
		ss << "error processing runs: " << e.what();
		throw_moea_error(ss.str());
	}
	catch (...)
	{
		throw_moea_error(string("error processing runs"));
	}
	//for testing
	//failed_real_indices.push_back(0);

	if (failed_real_indices.size() > 0)
	{
		stringstream ss;
		vector<string> par_real_names = _pe.get_real_names();
		vector<string> obs_real_names = _oe.get_real_names();
		ss << "the following par:obs realization runs failed: ";
		for (auto& i : failed_real_indices)
		{
			ss << par_real_names[i] << ":" << obs_real_names[i] << ',';
		}
		performance_log->log_event(ss.str());
		message(1, "failed realizations: ", failed_real_indices.size());
		string s = ss.str();
		message(1, s);
		performance_log->log_event("dropping failed realizations");
		_pe.drop_rows(failed_real_indices);
		_oe.drop_rows(failed_real_indices);
	}
	return failed_real_indices;
}

void MOEA::finalize()
{

}

void MOEA::initialize()
{
	stringstream ss;
	message(0, "initializing MOEA process");
	
	pp_args = pest_scenario.get_pestpp_options().get_passed_args();

	act_obs_names = pest_scenario.get_ctl_ordered_nz_obs_names();
	act_par_names = pest_scenario.get_ctl_ordered_adj_par_names();

	

	if (pest_scenario.get_control_info().noptmax == 0)
	{
		message(0, "'noptmax'=0, running control file parameter values and quitting");

		Parameters pars = pest_scenario.get_ctl_parameters();
		ParamTransformSeq pts = pest_scenario.get_base_par_tran_seq();

		ParameterEnsemble _pe(&pest_scenario, &rand_gen);
		_pe.reserve(vector<string>(), pest_scenario.get_ctl_ordered_par_names());
		_pe.set_trans_status(ParameterEnsemble::transStatus::CTL);
		_pe.append("BASE", pars);
		string par_csv = file_manager.get_base_filename() + ".par.csv";
		//message(1, "saving parameter values to ", par_csv);
		//_pe.to_csv(par_csv);
		ParameterEnsemble pe_base = _pe;
		pe_base.reorder(vector<string>(), act_par_names);
		ObservationEnsemble _oe(&pest_scenario, &rand_gen);
		_oe.reserve(vector<string>(), pest_scenario.get_ctl_ordered_obs_names());
		_oe.append("BASE", pest_scenario.get_ctl_observations());
		ObservationEnsemble oe_base = _oe;
		oe_base.reorder(vector<string>(), act_obs_names);
		//initialize the phi handler
		Covariance parcov;
		parcov.from_parameter_bounds(pest_scenario, file_manager.rec_ofstream());
		L2PhiHandler ph(&pest_scenario, &file_manager, &oe_base, &pe_base, &parcov);
		if (ph.get_lt_obs_names().size() > 0)
		{
			message(1, "less_than inequality defined for observations: ", ph.get_lt_obs_names().size());
		}
		if (ph.get_gt_obs_names().size())
		{
			message(1, "greater_than inequality defined for observations: ", ph.get_gt_obs_names().size());
		}
		message(1, "running control file parameter values");

		vector<int> failed_idxs = run_population(_pe, _oe);
		if (failed_idxs.size() != 0)
		{
			message(0, "control file parameter value run failed...bummer");
			throw_moea_error(string("control file parameter value run failed"));
		}
		string obs_csv = file_manager.get_base_filename() + ".obs.csv";
		message(1, "saving results from control file parameter value run to ", obs_csv);
		_oe.to_csv(obs_csv);

		ph.update(_oe, _pe);
		message(0, "control file parameter phi report:");
		ph.report(true);
		ph.write(0, 1);
		save_base_real_par_rei(pest_scenario, _pe, _oe, output_file_writer, file_manager, -1);
		return;
	}

	//set some defaults
	PestppOptions* ppo = pest_scenario.get_pestpp_options_ptr();

	//process dec var args
	vector<string> dec_var_groups = ppo->get_opt_dec_var_groups();
	if (dec_var_groups.size() != 0)
	{
		//first make sure all the groups are actually listed in the control file
		vector<string> missing;
		vector<string> pst_groups = pest_scenario.get_ctl_ordered_par_group_names();
		vector<string>::iterator end = pst_groups.end();
		vector<string>::iterator start = pst_groups.begin();
		for (auto grp : dec_var_groups)
			if (find(start, end, grp) == end)
				missing.push_back(grp);
		if (missing.size() > 0)
		{
			ss.str("");
			ss << "the following ++opt_dec_var_groups were not found: ";
			for (auto m : missing)
				ss << m << ",";
			throw_moea_error(ss.str());
		}


		//find the parameter in the dec var groups
		ParameterGroupInfo pinfo = pest_scenario.get_base_group_info();
		string group;
		end = dec_var_groups.end();
		start = dec_var_groups.begin();
		for (auto& par_name : pest_scenario.get_ctl_ordered_par_names())
		{
			group = pinfo.get_group_name(par_name);
			if (find(start, end, group) != end)
			{
				dv_names.push_back(par_name);

			}
		}

		if (dv_names.size() == 0)
		{
			ss.str("");
			ss << "no decision variables found in supplied dec var groups : ";
			for (auto g : dec_var_groups)
			{
				ss << g << ",";
			}
			throw_moea_error(ss.str());
		}
	}
	//otherwise, just use all adjustable parameters as dec vars
	else
	{
		dv_names = act_par_names;
	}


	iter = 0;
	member_count = 0;
	
	warn_min_members = 20;
	error_min_members = 4;
	
	message(1, "max run fail: ", ppo->get_max_run_fail());

	//TODO: deal with prior info objectives
	Constraints::ConstraintSense gt = Constraints::ConstraintSense::greater_than, lt = Constraints::ConstraintSense::less_than;
	pair<Constraints::ConstraintSense, string> sense;
	map<string, string> obj_sense_map;
	vector<string> onames = pest_scenario.get_ctl_ordered_nz_obs_names();
	if (obj_names.size() == 0)
	{
		for (auto oname : onames)
		{
			sense = Constraints::get_sense_from_group_name(pest_scenario.get_ctl_observation_info().get_group(oname));
			if (sense.first == gt)
			{
				obj_names.push_back(oname);
				obj_sense_map[oname] = "maximize";
				obj_dir_mult[oname] = -1.0;
			}
			else if (sense.first == lt)
			{
				obj_names.push_back(oname);
				obj_sense_map[oname] = "minimize";
				obj_dir_mult[oname] = 1.0;
			}
		}

		message(1, "'mou_objectives' not passed, using all nonzero weighted obs that use the proper obs group naming convention");
	}
	else
	{
		vector<string> onames = pest_scenario.get_ctl_ordered_nz_obs_names();
		set<string> oset(onames.begin(), onames.end());
		onames.clear();
		vector<string> missing,keep,err_sense;
		for (auto obj_name : obj_names)
		{
			if (oset.find(obj_name) == oset.end())
				missing.push_back(obj_name);
			else
			{
				sense = Constraints::get_sense_from_group_name(pest_scenario.get_ctl_observation_info().get_group(obj_name));
				if ((sense.first != gt) && (sense.first != lt))
					err_sense.push_back(obj_name);
				else
				{
					if (sense.first == gt)
					{
						keep.push_back(obj_name);
						obj_sense_map[obj_name] = "maximize";
						obj_dir_mult[obj_name] = -1.0;
					}
					else if (sense.first == lt)
					{
						keep.push_back(obj_name);
						obj_sense_map[obj_name] = "minimize";
						obj_dir_mult[obj_name] = 1.0;
					}
					
				}
			}
		}
		if (err_sense.size() > 0)
		{
			ss.str("");
			ss << "the following non-zero weighted 'mou_objectives' do not have the correct obs group naming convention (needed to identify objective direction):";
			for (auto e : err_sense)
				ss << e << ";";
			throw_moea_error(ss.str());
		}
		if (keep.size() == 0)
		{
			throw_moea_error("none of the supplied 'mou_objectives' were found in the zero-weighted observations");
		}
		
		else if (missing.size() > 0)
		{
			ss.str("");
			ss << "WARNING: the following mou_objectives were not found in the zero-weighted observations: ";
			for (auto m : missing)
				ss << m << ",";
			message(1, ss.str());

		}
		obj_names = keep;
	}

	ss.str("");
	ss << "...using the following observations as objectives: " << endl;
	for (auto s : obj_sense_map)
	{
		ss << setw(30) << s.first << "   " << s.second << endl;
	}
	file_manager.rec_ofstream() << ss.str();
	cout << ss.str();

	if (obj_names.size() > 5)
		message(1, "WARNING: more than 5 objectives, this is pushing the limits!");

	sanity_checks();


	//initialize the constraints using ctl file pars and obs
	//throughout the process, we can update these pars and obs
	//to control where in dec var space the stack/fosm estimates are
	//calculated
	effective_constraint_pars = pest_scenario.get_ctl_parameters();
	effective_constraint_obs = pest_scenario.get_ctl_observations();
	constraints.initialize(dv_names, &effective_constraint_pars, &effective_constraint_obs, numeric_limits<double>::max());


	int num_members = pest_scenario.get_pestpp_options().get_mou_population_size();
	population_dv_file = ppo->get_mou_dv_population_file();
	population_obs_restart_file = ppo->get_mou_obs_population_restart_file();
	
	initialize_dv_population();
	
	initialize_obs_restart_population();
	
	try
	{
		dp.check_for_dups();
	}
	catch (const exception& e)
	{
		string message = e.what();
		throw_moea_error("error in dv population: " + message);
	}

	try
	{
		op.check_for_dups();
	}
	catch (const exception& e)
	{
		string message = e.what();
		throw_moea_error("error in obs population: " + message);
	}


	//we are restarting
	if (population_obs_restart_file.size() > 0)
	{
	
		//since mou reqs strict linking of realization names, let's see if we can find an intersection set 
		vector<string> temp = dp.get_real_names();
		set<string> dvnames(temp.begin(), temp.end());
		temp = op.get_real_names();
		set<string> obsnames(temp.begin(), temp.end());
		set<string> common;
		set_intersection(dvnames.begin(), dvnames.end(), obsnames.begin(), obsnames.end(),std::inserter(common,common.end()));
		
		// all members are common to both dp and op
		if (common.size() == dp.shape().first)
		{
			op.reorder(dp.get_real_names(), vector<string>());
		}
		
		//otherwise some members are not common
		else
		{
			ss.str("");
			ss << "WARNING: only " << common.size() << " members are common between the dv population and obs restart population.";
			message(1, ss.str());
			if (common.size() < error_min_members)
			{
				throw_moea_error("too few members to continue");
			}
			
			message(2,"aligning dv and obs populations");
			temp.clear();
			temp.resize(common.size());
			copy(common.begin(), common.end(), temp.begin());
			sort(temp.begin(), temp.end());
			dp.reorder(temp, vector<string>(), true);
			op.reorder(temp, vector<string>(), true);
			message(2, "dv population size: ", dp.shape().first);
			message(2, "obs population size", op.shape().first);
			message(2, "checking for denormal values in dv population");
			dp.check_for_normal("initial transformed dv population");
			message(2, "checking for denormal values in obs restart population");
			op.check_for_normal("restart obs population");
		}
		//TODO: make any risk runs that need to be done here or do we assume the 
		//restart population has already been shifted?

		//TODO: save both sim and sim+chance observation populations
		
	}
	else
	{
		message(2, "checking for denormal values in dv population");
		dp.check_for_normal("initial transformed dv population");
		//save the initial population once here
		ss.str("");
		ss << file_manager.get_base_filename() << ".0." << dv_pop_file_tag << ".csv";
		dp.to_csv(ss.str());
		message(1, "saved initial dv population to ", ss.str());
		performance_log->log_event("running initial ensemble");
		message(1, "running initial ensemble of size", dp.shape().first);
		//TODO: add risk runs to run mgr queue
		vector<int> failed = run_population(dp, op);
		if (dp.shape().first == 0)
			throw_moea_error(string("all members failed during initial population evaluation"));
		//TODO: process risk runs, shift obj and constraint values
		dp.transform_ip(ParameterEnsemble::transStatus::NUM);
	}
	ss.str("");
	ss << file_manager.get_base_filename() << ".0." << obs_pop_file_tag << ".csv";
	op.to_csv(ss.str());
	message(1, "saved observation population to ", ss.str());

	//save the initial dv population again in case runs failed or members were dropped as part of restart
	ss.str("");
	ss << file_manager.get_base_filename() << ".0." << dv_pop_file_tag << ".csv";
	dp.to_csv(ss.str());
	message(1, "saved initial dv population to ", ss.str());

	//TODO: think about a bad phi (or phis) for MOEA
	
	if (op.shape().first <= error_min_members)
	{
		message(0, "too few population members:", op.shape().first);
		message(1, "need at least ", error_min_members);
		throw_moea_error(string("too few active population members, cannot continue"));
	}
	if (op.shape().first < warn_min_members)
	{
		ss.str("");
		ss << "WARNING: less than " << warn_min_members << " active population members...might not be enough";
		string s = ss.str();
		message(0, s);
	}


	//do an initial pareto dominance sort
	message(1, "performing initial pareto dominance sort");
	DomPair dompair = objectives.pareto_dominance_sort(obj_names, op, dp, obj_dir_mult);
	
	//initialize op and dp archives
	op_archive = ObservationEnsemble(&pest_scenario, &rand_gen, 
		op.get_eigen(dompair.first, vector<string>()),dompair.first,op.get_var_names());
	
	dp_archive = ParameterEnsemble(&pest_scenario, &rand_gen,
		dp.get_eigen(dompair.first, vector<string>()), dompair.first, dp.get_var_names());
	ss.str("");
	ss << "initialized archives with " << dompair.first.size() << " nondominated members";
	message(2, ss.str());
	archive_size = ppo->get_mou_max_archive_size();

	//ad hoc archive update test
	/*vector<string> temp = op.get_real_names();
	for (auto& m : temp)
		m = "XXX" + m;
	op.set_real_names(temp);
	op.set_eigen(*op.get_eigen_ptr() * 2.0);
	dp.set_real_names(temp);
	update_archive(op, dp);*/

	message(0, "initialization complete");
}


void MOEA::iterate_to_solution()
{
	stringstream ss;
	ofstream &frec = file_manager.rec_ofstream();

	for (int i = 0; i < pest_scenario.get_control_info().noptmax; i++)
	{

			//generate offspring

			//run offspring thru the model while also running risk runs, possibly at many points in dec var space

			//risk shift obj and constraint values, updating op in place

			//append offspring dp and (risk-shifted) op to make new dp and op containers

			//sort according to pareto dominance, crowding distance, and, eventually, feasibility

			//drop shitty members
	}

}


bool MOEA::initialize_dv_population()
{
	stringstream ss;
	int num_members = pest_scenario.get_pestpp_options().get_mou_population_size();
	string dv_filename = pest_scenario.get_pestpp_options().get_mou_dv_population_file();
	bool drawn = false;
	if (dv_filename.size() == 0)
	{
		ofstream& frec = file_manager.rec_ofstream();
		message(1, "drawing initial dv population of size: ", num_members);
		Parameters draw_par = pest_scenario.get_ctl_parameters();
		
		dp.draw_uniform(num_members, dv_names, performance_log, 1, file_manager.rec_ofstream());
		drawn = true;
	}
	else
	{
		string par_ext = pest_utils::lower_cp(dv_filename).substr(dv_filename.size() - 3, dv_filename.size());
		performance_log->log_event("processing dv population file " + dv_filename);
		if (par_ext.compare("csv") == 0)
		{
			message(1, "loading dv population from csv file", dv_filename);
			try
			{
				dp.from_csv(dv_filename);
			}
			catch (const exception& e)
			{
				ss << "error processing dv population file: " << e.what();
				throw_moea_error(ss.str());
			}
			catch (...)
			{
				throw_moea_error(string("error processing dv population file"));
			}
		}
		else if ((par_ext.compare("jcb") == 0) || (par_ext.compare("jco") == 0))
		{
			message(1, "loading dv population from binary file", dv_filename);
			try
			{
				dp.from_binary(dv_filename);
			}
			catch (const exception& e)
			{
				ss << "error processing binary file: " << e.what();
				throw_moea_error(ss.str());
			}
			catch (...)
			{
				throw_moea_error(string("error processing binary file"));
			}
		}
		else
		{
			ss << "unrecognized dv population file extension " << par_ext << ", looking for csv, jcb, or jco";
			throw_moea_error(ss.str());
		}

		dp.transform_ip(ParameterEnsemble::transStatus::NUM);
		ss.str("");
		ss << "dv population with " << dp.shape().first << " members read from '" << dv_filename << "'" << endl;
		message(1, ss.str());

		if (dp.shape().first == 0)
		{
			throw_moea_error("zero members found in dv population file");
		}

		if (pp_args.find("MOU_POPULATION_SIZE") != pp_args.end())
		{
			int num_members = pest_scenario.get_pestpp_options().get_mou_population_size();
			
			if (num_members < dp.shape().first)
			{
				message(1, "'mou_population_size' arg passed, truncated dv population to ", num_members);
				vector<string> keep_names, real_names = dp.get_real_names();
				for (int i = 0; i < num_members; i++)
				{
					keep_names.push_back(real_names[i]);
				}
				dp.keep_rows(keep_names);
			}
		}
		
	}
	return drawn;

}


void MOEA::initialize_obs_restart_population()
{
	string obs_filename = pest_scenario.get_pestpp_options().get_mou_obs_population_restart_file();
	if (obs_filename.size() == 0)
	{
		op.reserve(dp.get_real_names(), pest_scenario.get_ctl_ordered_obs_names());
		return;
	}
	stringstream ss;
	//throw_moea_error(string("restart obs population not implemented"));
	string par_ext = pest_utils::lower_cp(obs_filename).substr(obs_filename.size() - 3, obs_filename.size());
	performance_log->log_event("processing obs population file " + obs_filename);
	if (par_ext.compare("csv") == 0)
	{
		message(1, "loading obs population from csv file", obs_filename);
		try
		{
			op.from_csv(obs_filename);
		}
		catch (const exception& e)
		{
			ss << "error processing obs population file: " << e.what();
			throw_moea_error(ss.str());
		}
		catch (...)
		{
			throw_moea_error(string("error processing obs population file"));
		}
	}
	else if ((par_ext.compare("jcb") == 0) || (par_ext.compare("jco") == 0))
	{
		message(1, "loading obs population from binary file", obs_filename);
		try
		{
			op.from_binary(obs_filename);
		}
		catch (const exception& e)
		{
			ss << "error processing obs population binary file: " << e.what();
			throw_moea_error(ss.str());
		}
		catch (...)
		{
			throw_moea_error(string("error processing obs population binary file"));
		}
	}
	else
	{
		ss << "unrecognized obs population restart file extension " << par_ext << ", looking for csv, jcb, or jco";
		throw_moea_error(ss.str());
	}

	ss.str("");
	ss << "obs population with " << op.shape().first << " members read from '" << obs_filename << "'" << endl;
	message(1, ss.str());
	if (op.shape().first == 0)
	{
		throw_moea_error("zero members found in obs population restart file");
	}


}