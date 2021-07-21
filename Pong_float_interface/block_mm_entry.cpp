#include "../block.h"
#include <iostream> 

extern "C"{
//input tile: LL=L1
void loadIn(float In[],  hls::stream<blockvec> &Inrows,const int LL, int it){
	#pragma HLS aggregate variable=Inrows
//	int A_tile_index = int(it/(BSIZE/BLOCK_SIZE));
	for (int i = 0; i < LL; i++){
     	blockvec tempA;
     	#pragma HLS aggregate variable=tempA
     	for (int j = 0; j < BSIZE; j++){
			#pragma HLS PIPELINE
			// tempA.a[j]=In[it*LL*BSIZE+i*BSIZE+j];
			tempA.a[j]=In[i*BSIZE+j];
	  	}
   		Inrows.write(tempA);
	}
}


//weight tile: LL=L1/L2/L3
void loadW(float W[], w1blockvec w1bram[], const int LL, const int LN, int it){
	#pragma HLS aggregate variable=w1bram
//	int B_tile_index = it%(BSIZE/BLOCK_SIZE);
	for (int i = 0; i < LL; i++){
		w1blockvec tempA;
		#pragma HLS aggregate variable=tempA
		for (int j = 0; j < LN/4; j++){
			#pragma HLS PIPELINE
			tempA.a[j]=W[it*LL*LN/4+i*LN/4+j];
		}
		// Wcols.write(tempA)
		w1bram[i]=tempA;
	}
}


//weight tile: LL=L1/L2/L3
void loadW2(float W[], w3blockvec w2bram[], const int LL, const int LN, int it){
//	int B_tile_index = it%(BSIZE/BLOCK_SIZE);
	for (int i = 0; i < LL; i++){
		w3blockvec tempA;
		#pragma HLS aggregate variable=tempA
		for (int j = 0; j < LN; j++){
			#pragma HLS PIPELINE
			tempA.a[j]=W[it*LL*LN+i*LN+j];
		}
		// Wcols.write(tempA)
		w2bram[i]=tempA;
	}
}


void loadDDR(float In[], float W[], hls::stream<blockvec> &Arows, w1blockvec w1bram[], const int LL,const int LN,int it){
	// #pragma INTERFACE variable=A
	// #pragma INTERFACE variable=B
	//Assumption: A and B are entire matrices SIZE*BLOCK_SIZE(e.g. blockvec size) tiles
	#pragma HLS DATAFLOW
	loadIn(In, Arows, LL,it);
	loadW(W, w1bram,LL,LN, it);
}


//Inrows: LL blcokvecs (each batchsize)
//Wcols: LL wblockvecs (each LN)
//Crows: LN blockvecs (each batchsize)
void blockmatmul(hls::stream<blockvec> &Inrows, hls::stream<blockvec> &Crows, w1blockvec Wcols[], const int LL,const int LN,int it) {
#pragma HLS aggregate variable=Inrows
#pragma HLS aggregate variable=Wcols
#pragma HLS aggregate variable=Crows
	// float C[BSIZE/P][512/T][P][T]={0}; //512 is the largest layer. change based on models
	// #pragma HLS ARRAY_PARTITION variable=C dim=3 complete
	// #pragma HLS ARRAY_PARTITION variable=C dim=4 complete
	static float C[BSIZE/P][512/T][P][T]; //512 is the largest layer. change based on models
	#pragma HLS ARRAY_PARTITION variable=C dim=3 complete
	#pragma HLS ARRAY_PARTITION variable=C dim=4 complete
	#pragma HLS bind_storage variable=C type=RAM_2P impl=bram
	if (it==0){
		for (int i=0;i< BSIZE/P; i++){
			for(int j = 0; j < 512/T; j++) {
				for(int ii = 0; ii < P; ii++) {
					#pragma HLS UNROLL
					for(int jj = 0; jj < T; jj++) {	
						#pragma HLS UNROLL
						C[i][j][ii][jj] = 0;
					}
				}
			}
		}
	}


	partialsum: for(int k=0; k < LL; k++) {
		blockvec tempA = Inrows.read();
		w1blockvec tempB = Wcols[k];
    #pragma HLS aggregate variable=tempA
     #pragma HLS aggregate variable=tempB
		for(int i = 0; i < BSIZE/P; i++) {
			#pragma HLS dependence array variable=C inter false
			for(int j = 0; j < LN/4/T; j++) {
			#pragma HLS PIPELINE
			#pragma HLS dependence array variable=C inter false
				for(int ii = 0; ii < P; ii++) {
					#pragma HLS UNROLL
					for(int jj = 0; jj < T; jj++) {
						#pragma HLS UNROLL
						//#pragma HLS dependence variable=C inter false
						C[i][it*LN/4/T+j][ii][jj] = C[i][it*LN/4/T+j][ii][jj] + tempA.a[i*P+ii] * tempB.a[j*T+jj];
					}
				}
			}
		}
	}
	//write out to stream
	if (it==3){
		for(int j = 0; j < LN/T; j++) {
			for(int jj = 0; jj < T; jj++) {
	     #pragma HLS PIPELINE
				blockvec tempC;
				#pragma HLS aggregate variable=tempC
				for(int i = 0; i < BSIZE/P; i++) {
					for(int ii = 0; ii < P; ii++) {
						tempC.a[i*P+ii]=C[i][j][ii][jj];
					}
				}
				Crows.write(tempC);
			}
		}
	}

}

void write_out_stream(float C[BSIZE/P][512/T][P][T], hls::stream<blockvec> &Crows,const int LN){

	for(int j = 0; j < LN/T; j++) {
		for(int jj = 0; jj < T; jj++) {
     #pragma HLS PIPELINE
			blockvec tempC;
			#pragma HLS aggregate variable=tempC
			for(int i = 0; i < BSIZE/P; i++) {
				for(int ii = 0; ii < P; ii++) {
					tempC.a[i*P+ii]=C[i][j][ii][jj];
				}
			}
			Crows.write(tempC);
		}
	}
}


//Inrows: LL blcokvecs (each batchsize)
//Wcols: LL wblockvecs (each LN)
//Crows: LN blockvecs (each batchsize)
void blockmatmul3(hls::stream<blockvec> &Inrows, w3blockvec Wcols[], hls::stream<blockvec> &Crows,const int LL,const int LN) {
#pragma HLS aggregate variable=Inrows
#pragma HLS aggregate variable=Wcols
#pragma HLS aggregate variable=Crows
	float C[BSIZE/P][2/T2][P2][T2]={0};
	#pragma HLS ARRAY_PARTITION variable=C dim=3 complete
	#pragma HLS ARRAY_PARTITION variable=C dim=4 complete
	#pragma HLS bind_storage variable=C type=RAM_2P impl=bram

	partialsum: for(int k=0; k < LL; k++) {
		blockvec tempA = Inrows.read();
		w3blockvec tempB = Wcols[k];
    #pragma HLS aggregate variable=tempA
     #pragma HLS aggregate variable=tempB
		for(int i = 0; i < BSIZE/P2; i++) {
			for(int j = 0; j < LN/T2; j++) {
			#pragma HLS PIPELINE
			#pragma HLS dependence variable=C inter false
				for(int ii = 0; ii < P2; ii++) {
					#pragma HLS UNROLL
					for(int jj = 0; jj < T2; jj++) { //3
						#pragma HLS UNROLL
						C[i][j][ii][jj] = C[i][j][ii][jj] + tempA.a[i*P2+ii] * tempB.a[j*T2+jj];
					}
				}
			}
		}
	}
	//write out to stream
	for(int j = 0; j < LN/T2; j++) {
		for(int jj = 0; jj < T2; jj++) {
   			#pragma HLS PIPELINE
			blockvec tempC;
			#pragma HLS aggregate variable=tempC
			for(int i = 0; i < BSIZE/P2; i++) {
				for(int ii = 0; ii < P2; ii++) {
					tempC.a[i*P2+ii]=C[i][j][ii][jj];
				}
			}
			Crows.write(tempC);
		}
	}
}

void activation(hls::stream<blockvec> &Inrows, float bias[], hls::stream<blockvec> &Outrows,const int L){
	for (int i = 0; i < L; i++){
		#pragma HLS PIPELINE
		blockvec temp = Inrows.read();
		blockvec temp_out;
		for (int j = 0; j < BSIZE; j++){
			#pragma HLS UNROLL
			// temp_out.a[j]=hls::tanh(temp.a[j]+bias[i]);
			// relu, not tanh
			temp_out.a[j]=(temp.a[j]+bias[i]<0)?0:temp.a[j]+bias[i];
		}
		Outrows.write(temp_out);
	}
}
void storeDDR(float O[],  hls::stream<blockvec> &Crows,  const int LN){
	for (int i = 0; i < LN; i++){
   //printf("In itr %d\n",i);
		blockvec tmp = Crows.read();
     for (int j = 0; j < BSIZE; j++){
     #pragma HLS PIPELINE
       O[i*BSIZE+j]=tmp.a[j];
     }
	}
 //printf("Yaaassss\n");

}

void top(float *A, float *B1,float *B2,float *O){
//#pragma HLS INTERFACE bram port=C storage_type=ram_2p
	//Put DDR interfacing directives for A & B
	#pragma HLS INTERFACE m_axi port=A bundle=gmem0 offset=slave
	#pragma HLS INTERFACE m_axi port=B1 bundle=gmem1 offset=slave
	#pragma HLS INTERFACE m_axi port=B2 bundle=gmem2 offset=slave
	#pragma HLS INTERFACE m_axi port=O bundle=gmem3 offset=slave
	#pragma HLS INTERFACE s_axilite port=A bundle=control
	#pragma HLS INTERFACE s_axilite port=B1 bundle=control
	#pragma HLS INTERFACE s_axilite port=B2 bundle=control
	#pragma HLS INTERFACE s_axilite port=O bundle=control
	#pragma HLS INTERFACE s_axilite port=return bundle=control

	
	//Assume C is buffered on-chip
//	blockmat C[SIZE*SIZE/(BLOCK_SIZE*BLOCK_SIZE)];
//#pragma HLS aggregate variable=C

	hls::stream<blockvec> inpipe;
	w1blockvec w1bram[L1_total];
	w3blockvec w2bram[L2];
	#pragma HLS bind_storage variable=w1bram type=RAM_1P impl=uram
	#pragma HLS bind_storage variable=w2bram type=RAM_1P impl=uram

  float bias1[L2]={-0.002713746391236782,0.005080224946141243,0.0004187696613371372,0.002765250625088811,-0.009916051290929317,-0.011488431133329868,-0.0013400388415902853,-0.021577484905719757,4.346558853285387e-05,0.005001293495297432,-0.006017204374074936,-0.004392923787236214,-0.006819071713835001,0.00638744980096817,0.00249408814124763,-0.01205851323902607,-0.004362733103334904,-0.018475547432899475,-0.01908177137374878,-0.007495387457311153,-0.006348637863993645,-0.006212342530488968,-0.0019430192187428474,-0.008549263700842857,0.002515411237254739,-0.0031618501525372267,-0.0045793126337230206,0.0073177204467356205,0.0014229478547349572,0.0048087965697050095,0.0015623789513483644,-0.012747623026371002,-0.009565400891005993,-0.00210772268474102,-0.011433745734393597,0.008184936828911304,-0.011166036128997803,-0.00013351303641684353,-0.00283531891182065,-0.005506355315446854,-0.007421841844916344,-0.002563793445006013,-0.025578731670975685,-0.02994558960199356,-0.006186700891703367,-0.0057435305789113045,-0.007784753106534481,0.003479316597804427,-0.008498371578752995,0.016285952180624008,0.012491601519286633,0.027105772867798805,-0.001695004990324378,0.003257863689213991,-0.012198065407574177,-0.005054636392742395,-0.005502903368324041,-0.008389001712203026,-0.0011754181468859315,0.005770880728960037,6.980852776905522e-05,-0.00970427691936493,-0.0026128734461963177,0.0032911242451518774,-0.0008171047666110098,-0.0019441660260781646,0.003921688999980688,0.008867966011166573,-0.009013532660901546,-0.004831379745155573,-0.01548770722001791,-0.004521734081208706,0.003369347658008337,0.00203329767100513,-0.01855839416384697,-0.00038492161547765136,0.0049302838742733,0.0027426721062511206,0.003629702841863036,0.013267380185425282,0.01220780611038208,0.018412014469504356,0.0008072922937572002,-0.01395778451114893,-0.0061768582090735435,-0.0023615718819200993,0.011616542004048824,-0.005581867881119251,-0.009748296812176704,-9.193787263939157e-05,-0.006887709256261587,-0.01023881509900093,-0.012862513773143291,0.004930448718369007,-0.010106847621500492,-0.00348519254475832,0.0015669467393308878,0.011902340687811375,0.0029599976260215044,0.00025102501967921853,-0.012430957518517971,-0.0045038750395178795,0.0002334681776119396,0.0063716997392475605,-0.00926326122134924,-0.0072345309890806675,-0.012615982443094254,-0.011489560827612877,0.0033125276677310467,0.004587702453136444,-0.00236687995493412,-0.0069780778139829636,0.0017995426896959543,-0.007602886762470007,0.002364694606512785,-0.014224877581000328,-0.00439787283539772,0.0016004437347874045,0.001497467397712171,0.003642771393060684,-0.008494473062455654,-0.010078531689941883,-0.010260190814733505,-0.0027102401945739985,-0.0015873651718720794,0.0016823416808620095,-0.0017960106488317251,-0.01867027021944523,-0.00453923037275672,-0.004059880971908569,-0.01189577579498291,0.0052947415970265865,-0.014916712418198586,0.003307005623355508,-0.008669333532452583,-0.0053834435530006886,-0.013174625113606453,0.005942160729318857,-0.0046415687538683414,-0.0017763469368219376,0.0006930945673957467,-0.01184218842536211,-0.013073526322841644,0.00035654802923090756,0.013783627189695835,0.003992204088717699,-0.006444474682211876,-0.014413918368518353,-0.013523286208510399,0.006855919025838375,-0.004742837511003017,-0.008915621787309647,-0.004507176112383604,-0.006434829439967871,-0.012650120072066784,-0.0004001902707386762,0.008143136277794838,0.009340690448880196,0.004880652297288179,-0.010304292663931847,-0.005832995288074017,0.0015519371954724193,-0.0090406509116292,-0.00734327919781208,0.002056376077234745,0.0024600259494036436,0.005347602069377899,-0.000256237864959985,0.00665899645537138,0.0022179163061082363,-0.007676857057958841,-0.018110964447259903,-0.005683806259185076,-0.006123387720435858,0.005140654742717743,-0.00837423000484705,-0.0231422558426857,-0.005560790188610554,-0.012345274910330772,0.010971656069159508,-0.007717514410614967,-0.0011895514326170087,-0.013156807981431484,0.0007771714590489864,-0.009255082346498966,0.0005370446597225964,-0.0015117593575268984,0.001212257775478065,0.0029095052741467953,-0.012441689148545265,-0.025410212576389313,0.0029722615145146847,-0.004348330199718475,0.0037932591512799263,0.002612097654491663,0.00026560985133983195,0.002458008239045739,-0.008994814939796925,0.0031473543494939804,-0.0005555519601330161,0.0016262870049104095,-0.004454184789210558,-0.0024945845361799,-0.012215862981975079,0.005945962853729725,0.004429275635629892,-0.0007699612760916352,-0.00417544599622488,0.0212148055434227,-0.005338952410966158,-0.009549399837851524,0.004751088097691536,0.0024963724426925182,9.22792823985219e-05,0.0009357861708849669,-0.004230358172208071,-0.007060748990625143,0.002019342500716448,0.007335562724620104,0.016144169494509697,-0.0006140721961855888,-0.007368006277829409,-0.006721615791320801,-0.016682691872119904,0.004941405728459358,-0.003667382290586829,0.0009203062509186566,-0.002565731294453144,0.0015367042506113648,-0.00734518188983202,-0.011258527636528015,-0.0040993099100887775,-0.006352203898131847,0.0013305011671036482,-0.0016849038656800985,-0.0005415490595623851,-0.010602817870676517,0.0007205692236311734,0.017739390954375267,-0.009496285580098629,-0.012094645760953426,0.0037481181789189577,-0.007096330635249615,-0.007805733475834131,0.009681282564997673,0.0023009555879980326,-0.005463696550577879,0.003927135374397039,-0.009062036871910095,0.0020932380575686693,-0.0064757694490253925,-0.013251649215817451,-0.008923092857003212,-0.01172816101461649,-0.018200665712356567,0.0005910075269639492,0.002729956526309252,-0.009828944690525532,0.0007334572728723288,-0.005272684618830681,0.0019034093711525202,-0.009643997997045517,0.0006993152783252299,-0.004222061950713396,0.008090300485491753,-0.004020598717033863,0.0037709688767790794,-0.014054608531296253,0.00930807739496231,-0.0005556044634431601,-0.0034010109957307577,-0.0035000103525817394,-0.006122499704360962,0.014579189009964466,0.0006013787351548672,-0.01384483091533184,-0.002896643243730068,-0.00017263612244278193,-0.0017051384784281254,-0.004914804827421904,-0.010413680225610733,-0.0004340493178460747,0.0028567754197865725,-0.011093140579760075,0.012673621065914631,-0.012767140753567219,0.002692590467631817,0.020954132080078125,-0.011112737469375134,-0.004005576483905315,0.018061043694615364,0.0015080058947205544,0.009264349937438965,-8.341179636772722e-05,-0.004826872609555721,-0.013858792372047901,-0.0020321302581578493,-0.008489825762808323,-0.016040310263633728,-0.027370847761631012,-0.0008242321782745421,-0.0010698335245251656,0.0010094555327668786,0.023174051195383072,-0.00761030800640583,-0.016256412491202354,-0.004868939518928528,-0.0025790182407945395,-0.0029468813445419073,-0.01679951883852482,0.021071920171380043,-0.012218340300023556,-0.011118500493466854,-0.0003757263475563377,6.361228588502854e-05,-0.0005979566485621035,-0.00022751049255020916,-0.00037130838609300554,-0.006670496892184019,-0.01253445167094469,-0.013846054673194885,-0.004852450918406248,-0.005305555649101734,-0.005610466469079256,-0.00569887924939394,0.007539889309555292,0.003869602456688881,-0.009564313106238842,-0.011090644635260105,0.0013227775925770402,8.519881521351635e-05,0.0002654888085089624,0.005184595938771963,-0.01293883752077818,-0.012772710993885994,-0.010802377946674824,-0.014802423305809498,-0.007657251786440611,-0.012798696756362915,-0.008196062408387661,-0.004450210835784674,-0.007372723426669836,0.0008577235275879502,-0.002298792125657201,-0.000953841779846698,-0.0018946272321045399,0.013363036327064037,-0.01653256081044674,-0.008914356119930744,-0.003550749970600009,0.0036228455137461424,-0.027089571580290794,-0.00912250205874443,-0.0018309613224118948,-0.0011959286639466882,-0.0030877050012350082,-0.0012742846738547087,0.006088309921324253,0.0006835042731836438,-0.011710245162248611,0.0030054799281060696,-0.012172117829322815,-0.01115301251411438,-0.002680460922420025,0.025446414947509766,-0.015589754097163677,-0.017598187550902367,-0.006947848945856094,0.0009117236477322876,0.0011831065639853477,-0.001697296160273254,-0.0006551197730004787,-0.0009963972261175513,-0.0021384120918810368,0.007906852290034294,-0.008182176388800144,-0.008406797423958778,-0.010525963269174099,0.0006602809298783541,-0.00524611072614789,-0.0031448150984942913,-0.011271439492702484,-0.004165921360254288,-0.0008157072588801384,-0.012333232909440994,-0.01010476890951395,0.003493712982162833,-0.015435216948390007,-0.00313038332387805,-0.010986369103193283,-0.007491046562790871,-0.011539550498127937,-0.014893166720867157,-0.01960090734064579,0.0007258176920004189,0.006656023673713207,-0.0011502125998958945,-0.011305021122097969,-0.00873297918587923,-0.007777730468660593,-0.0001740415027597919,-0.011878843419253826,0.005202293861657381,0.0004587218281812966,-0.013501821085810661,-0.011588663794100285,0.0012447163462638855,-0.01930023916065693,-0.0018862563883885741,-0.00185905781108886,-0.012135524302721024,0.00854549277573824,-0.0005963470903225243,-0.005243820603936911,0.007017169147729874,0.0160627793520689,-0.006062440574169159,-0.004887605085968971,0.008213193155825138,0.0008436907664872706,-0.0032176650129258633,-0.00755520211532712,0.0003476886195130646,0.009846647270023823,-0.001437547616660595,-0.010721147991716862,0.004216266795992851,-0.00012414055527187884,0.0002578997518867254,-0.00817043799906969,-0.013877129182219505,-0.00720814848318696,0.004710044711828232,-0.0035767043009400368,-0.013726460747420788,0.001896757516078651,-0.0010663399007171392,-0.009214978665113449,-0.010121658444404602,-0.025439683347940445,-0.012232397682964802,-0.0007515224860981107,0.01366118062287569,0.0007641977863386273,-0.017699338495731354,-0.005948689766228199,-0.0031652143225073814,-0.006928900722414255,-0.010590810328722,-0.0004730664659291506,0.0057754479348659515,-0.0068548512645065784,-0.013295218348503113,-0.0011989913182333112,-0.0045437877997756,-0.005967109464108944,-0.004460780881345272,-0.004731801338493824,-0.01027575321495533,0.0032469863072037697,-0.004282424226403236,-0.003989067394286394,-0.00924102496355772,-0.00572930509224534,-0.018125521019101143,-0.0034397768322378397,0.001726677524857223,7.398384332191199e-05,-0.011245423927903175,-0.010572497732937336,-0.0035776817239820957,-0.003064611693844199,-0.014875960536301136,-0.014208712615072727,0.0009159045876003802,-0.0037565992679446936,-0.012304951436817646,-0.036240242421627045,-0.005813682917505503,-0.004276437684893608,-0.001130161457695067,-0.012913296930491924,0.007590222172439098,0.005075267981737852,-0.00034683311241678894,-0.016202250495553017,-0.0020443496759980917,-0.0017630856018513441,0.0076440442353487015,-0.0009424259187653661,-0.003915851004421711,0.0009002761216834188,0.001025890582241118,0.008911875076591969,-0.010274655185639858,-0.0006980486796237528,-0.009437421336770058,-0.0002493816427886486,-0.0128041822463274,0.00016224163118749857,-0.010896950028836727,-0.0044518220238387585,0.02322024293243885,0.002736307680606842,-0.010126377455890179,0.020640231668949127,-0.014071434736251831,0.010566274635493755,-0.0005696375737898052,-0.0089614512398839,-0.007708959747105837,-0.00012240279465913773};
  float bias2[L3]={0.018759336322546005,0.0012862527510151267};
	#pragma HLS bind_storage variable=bias1 type=RAM_1P impl=bram
	#pragma HLS bind_storage variable=bias2 type=RAM_1P impl=bram
	// float C1[BSIZE/P][512/T][P][T]={0}; //512 is the largest layer. change based on models
	// #pragma HLS ARRAY_PARTITION variable=C1 dim=3 complete
	// #pragma HLS ARRAY_PARTITION variable=C1 dim=4 complete
	// #pragma HLS bind_storage variable=C1 type=RAM_2P impl=bram


	hls::stream<blockvec> outpipe[3];
	//hls::stream<blockvec> actpipe[3];
	#pragma HLS STREAM variable=inpipe depth=64
	#pragma HLS STREAM variable=outpipe depth=512

//	Init on-chip memory

{	
  #pragma HLS DATAFLOW
	// for (int it=0; it<L1_total/L1;it++){
	for (int it=0; it<4;it++){
		#pragma HLS DATAFLOW
		// need a iteration number in the argument! - it
		// loadDDR(A, B1, inpipe, w1bram, L1,L2,it);
		loadDDR(A, B1, inpipe, w1bram, L1_total,L2,it);
		printf("load 1\n");
		// blockmatmul(inpipe, w1bram, C1, L1,L2);
		blockmatmul(inpipe, outpipe[0],w1bram, L1_total,L2,it);
		printf("MM 1\n");
	}
	// write_out_stream(C1, outpipe[0],L2);
 
//	loadW(w1blockvec W[], wpipe[1], L1);
	
 //printf("MM 1\n");
//	blockmatmul(outpipe[0], w2bram, outpipe[1],L2,L3);

  activation(outpipe[0], bias1, outpipe[1],L2);
  printf("activation 1\n");
  loadW2(B2, w2bram, L2,L3,0);
	blockmatmul3(outpipe[1], w2bram, outpipe[2],L2,L3);
  printf("MM 2\n");
  // activation(outpipe[2], bias2, outpipe[3],L3);
  printf("activation 2\n");
	storeDDR(O, outpipe[2], L3);
  printf("kernel really finished\n");
}
}
}



