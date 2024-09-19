#include "../callbacks.h"

int main(int argc, char** argv){
    bool debug=0;
    Disturbance target1;
    vertexDescriptor solution=TransitionSystem::null_vertex();
    if (argc>2){
        solution=vertexDescriptor(atoi(argv[2]));
    }
    else{
        throw;
    }
    if (argc>3){
        if (atoi(argv[3])==1){
            target1= Disturbance(PURSUE, b2Vec2(1.0,0), 0);    
        }
    }
    Task goal(target1,DEFAULT);
    Configurator conf(goal);
    conf.simulationStep=0.27;
    ConfiguratorInterface ci;
    conf.registerInterface(&ci);
    DataInterface di(&ci);
    if (argc>1){
        di.folder=argv[1];
        di.newScanAvail();          
    }
    conf.data2fp = ci.data2fp;
    conf.addIteration();
    b2World world(b2Vec2(0,0));
    boost::clear_vertex(conf.movingVertex, conf.transitionSystem);
    conf.dummy_vertex(conf.currentVertex);
    conf.explorer(conf.currentVertex, conf.transitionSystem, *conf.getTask(), world);
    conf.planVertices= conf.planner(conf.transitionSystem, conf.currentVertex);
    printf("OG PLAN: ");
    conf.printPlan(&conf.planVertices);
    conf.currentVertex=*(conf.planVertices.end()-1);
    std::vector <vertexDescriptor> options_src;
    State state_tmp;
    b2Transform shift= b2Transform(b2Vec2(1,0), b2Rot(0));
    conf.applyAffineTrans(shift, conf.transitionSystem);    
    if (argc>4){
        di.iteration=atoi(argv[4]);
        di.newScanAvail();          
        conf.data2fp = ci.data2fp;
    }
    std::vector <BodyFeatures> b_features=conf.worldBuilder.getFeatures(conf.data2fp, state_tmp.start, DEFAULT, BOX2DRANGE);
    if (!b_features.empty()){
        state_tmp.disturbance= Disturbance(b_features[0]); //assumes 1 item length
    }
    bool relax_match=1, plan_works=false;
    boost::clear_vertex(conf.movingVertex, conf.transitionSystem);
    conf.findMatch(state_tmp,conf.transitionSystem, NULL, UNDEFINED, StateMatcher::DISTURBANCE, &options_src, relax_match);
    std::vector<vertexDescriptor> plan_provisional;
    for (vertexDescriptor o: options_src){
        auto srcs= gt::inEdges(conf.transitionSystem, o);
        vertexDescriptor o_src=o;
        if(!srcs.empty()){
            o_src= srcs[0].m_source;
        }
        b2Transform o_shift= -conf.transitionSystem[o_src].endPose;
        Task controlGoal_tmp= conf.controlGoal;
        conf.applyAffineTrans(o_shift, controlGoal_tmp);

        plan_provisional=conf.planner(conf.transitionSystem, o_src, TransitionSystem::null_vertex(), false, &controlGoal_tmp); //been.second, been.first
        auto vi= (plan_provisional.end()-1);
        vertexDescriptor end =*(vi);
        bool ctrl_finished = controlGoal_tmp.checkEnded(conf.transitionSystem[end]).ended;
        plan_works= conf.checkPlan(world, plan_provisional, conf.transitionSystem,  conf.transitionSystem[*plan_provisional.begin()].start,o_src);
        if (ctrl_finished){
            if (plan_works){
                break;
            }
        }
        plan_provisional.clear();
    }
    
    if (plan_provisional!=conf.planVertices){
        printf("PROVISIONAL: ");
        conf.printPlan(&plan_provisional);

        return 1;
    }
    return 0;
}