/**
 * ofxTimeline
 * openFrameworks graphical timeline addon
 *
 * Copyright (c) 2011-2012 James George
 * Development Supported by YCAM InterLab http://interlab.ycam.jp/en/
 * http://jamesgeorge.org + http://flightphase.com
 * http://github.com/obviousjim + http://github.com/flightphase
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "ofxTimeline.h"
#include "ofxHotKeys.h"
#ifdef TARGET_OSX
#include "ofxRemoveCocoaMenu.h"
#endif

//increments to keep auto generated names unique
static int timelineNumber = 0;

bool headersort(ofxTLTrackHeader* a, ofxTLTrackHeader* b){
	return a->getDrawRect().y < b->getDrawRect().y;
}

#define TAB_HEIGHT 18
#define TICKER_HEIGHT 27
#define ZOOMER_HEIGHT 14
#define INOUT_HEIGHT 7

ofxTimeline::ofxTimeline()
:	width(1024),
	offset(ofVec2f(0,0)),
	autosave(true),
	isFrameBased(false),
	timelineHasFocus(false),
	showTicker(true), 
	showInoutControl(true),
	showZoomer(true),
	durationInSeconds(100.0f/30.0f),
	isShowing(true),
	isSetup(false),
	usingEvents(false),
	isPlaying(false),
	isEnabled(false),
	dragAnchorSet(false),
	snapToBPM(false),
	snapToOtherElements(true),
	dragMillsecondOffset(0),
	movePlayheadOnPaste(true),
	movePlayheadOnDrag(false),
	inoutRange(ofRange(0.0,1.0)),
	currentPage(NULL),
	modalTrack(NULL),
	timeControl(NULL),
	loopType(OF_LOOP_NONE),
	lockWidthToWindow(true),
	currentTime(0.0),
	undoPointer(0),
	undoEnabled(true),
	isOnThread(false),
	unsavedChanges(false),
	curvesUseBinary(false),
	headersAreEditable(false),
	//copy from ofxTimeline/assets into bin/data/
	defaultPalettePath("GUI/defaultColorPalette.png"),
	//TODO: should be able to use bitmap font if need be
	fontPath("GUI/NewMedia Fett.ttf"),
	fontSize(9)
{
}

ofxTimeline::~ofxTimeline(){
	if(isSetup){
		ofRemoveListener(timelineEvents.viewWasResized, this, &ofxTimeline::viewWasResized);
		ofRemoveListener(timelineEvents.pageChanged, this, &ofxTimeline::pageChanged);

        //TODO: move to shared pointers 
        //this breaks timelines that are statically declared because 
        //there is no copy/assignment constructor
		reset();
        
        delete ticker;
        delete tabs;
        delete zoomer;
	}
}

void ofxTimeline::setup(){

	isSetup = true;
	
	width = ofGetWidth();

	tabs = new ofxTLPageTabs();
	tabs->setTimeline(this);
	tabs->setup();
	tabs->setDrawRect(ofRectangle(offset.x, offset.y, width, TAB_HEIGHT));

    inoutTrack = new ofxTLInOut();
    inoutTrack->setTimeline(this);
    inoutTrack->setDrawRect(ofRectangle(offset.x, tabs->getBottomEdge(), width, INOUT_HEIGHT));
    
	ticker = new ofxTLTicker();
	ticker->setTimeline(this);
	
    //TODO: save ticker playhead position
	ticker->setup();
	ticker->setDrawRect(ofRectangle(offset.x, inoutTrack->getBottomEdge(), width, TICKER_HEIGHT));
	
	zoomer = new ofxTLZoomer();
	zoomer->setTimeline(this);
	zoomer->setDrawRect(ofRectangle(offset.y, ticker->getBottomEdge(), width, ZOOMER_HEIGHT));
	
	colors.load();

	enable();
    
	ofAddListener(timelineEvents.viewWasResized, this, &ofxTimeline::viewWasResized);
	ofAddListener(timelineEvents.pageChanged, this, &ofxTimeline::pageChanged);
	ofAddListener(ofEvents().update, this, &ofxTimeline::update);

    //You can change this name by calling setPageName()
	addPage("Page One", true);
    
	if(name == ""){
	    setName("timeline" + ofToString(timelineNumber++));
	}
	else{
		setupStandardElements();
	}

}

void ofxTimeline::moveToThread(){
	if(!isOnThread){
		stop();
		isOnThread = true;
		ofRemoveListener(ofEvents().update, this, &ofxTimeline::update);
		ofAddListener(ofEvents().exit, this, &ofxTimeline::exit);
		startThread();
	}
}

void ofxTimeline::removeFromThread(){
	if(isOnThread){
		stop();
		isOnThread = false;
		ofAddListener(ofEvents().update, this, &ofxTimeline::update);
		ofRemoveListener(ofEvents().exit, this, &ofxTimeline::exit);
		waitForThread(true);
	}
}

void ofxTimeline::setName(string newName){
    if(newName != name){
        
	    name = newName;
		if(isSetup){
			setupStandardElements();
		}
    }
}

void ofxTimeline::setupStandardElements(){
	
	inoutTrack->setXMLFileName( ofToDataPath(workingFolder + name + "_inout.xml") );
	inoutTrack->setup();
	
	zoomer->setXMLFileName( ofToDataPath(workingFolder + name + "_zoomer.xml") );
	zoomer->setup();
	
	currentPage->loadTrackPositions();	
}

string ofxTimeline::getName(){
    return name;
}

void ofxTimeline::setWorkingFolder(string folderPath){
	workingFolder = folderPath = ofFilePath::addTrailingSlash(folderPath);
    inoutTrack->setXMLFileName( ofToDataPath(workingFolder + name + "_inout.xml") );
	inoutTrack->load();
    zoomer->setXMLFileName( ofToDataPath(workingFolder + name + "_zoomer.xml") );
	zoomer->load();
	
}

string ofxTimeline::getWorkingFolder(){
    return workingFolder;
}

void ofxTimeline::loadTracksFromFolder(string folderPath){
    for(int i = 0; i < pages.size(); i++){
        pages[i]->loadTracksFromFolder(folderPath);
    }
}

#pragma mark CONFIGURATION
void ofxTimeline::show(){
	isShowing = true;
}
	
void ofxTimeline::hide(){
	isShowing = false;
}

bool ofxTimeline::toggleShow(){
	isShowing = !isShowing;
	return isShowing;
}

void ofxTimeline::setShowTimeControls(bool shouldShowTimeControls){
    setShowInoutControl(shouldShowTimeControls);
    setShowZoomer(shouldShowTimeControls);
    setShowTicker(shouldShowTimeControls);
}

void ofxTimeline::setShowTicker(bool shouldShowTicker){
    showTicker = shouldShowTicker;
    recalculateBoundingRects();
}

void ofxTimeline::setShowInoutControl(bool shouldShowInoutControl){
    showInoutControl = shouldShowInoutControl;
    recalculateBoundingRects();
}

void ofxTimeline::setShowZoomer(bool shouldShowZoomer){
    showZoomer = shouldShowZoomer;
    if(showZoomer){
		zoomer->load();        
    }
    else{
        zoomer->setViewRange(ofRange(0,1.0));
    }
    recalculateBoundingRects();
}

void ofxTimeline::setupFont(){
	font.loadFont(fontPath, fontSize);
}

void ofxTimeline::setupFont(string newFontPath, int newFontSize){
	fontPath = newFontPath;
	fontSize = newFontSize;
	setupFont();
}

OFX_TIMELINE_FONT_RENDERER & ofxTimeline::getFont(){
	if(!font.isLoaded()){
		setupFont();
	}
	return font;
}

ofxTLColors& ofxTimeline::getColors(){
	return colors;
}

void ofxTimeline::setMovePlayheadOnPaste(bool move){
	movePlayheadOnPaste = move;
}

bool ofxTimeline::getMovePlayheadOnPaste(){
	return movePlayheadOnPaste;
}

vector<string>& ofxTimeline::getPasteboard(){
	return pasteboard;
}

//turn on undo
void ofxTimeline::enableUndo(bool enabled){
	undoEnabled = enabled;    
}

void ofxTimeline::undo(){
    if(undoPointer > 0){
    	undoPointer--;
        restoreToState(undoStack[undoPointer]);
		unsavedChanges = true;		
    }
}

void ofxTimeline::redo(){
    if(undoPointer < undoStack.size()-1){
        undoPointer++;
		restoreToState(undoStack[undoPointer]);
		unsavedChanges = true;
    }
}

void ofxTimeline::restoreToState(vector<UndoItem>& state){
    for(int i = 0; i < state.size(); i++){
//		cout << "restoring state for track " << state[i].track->getDisplayName() << endl;
//		cout << state[i].stateBuffer << endl;
        state[i].track->loadFromXMLRepresentation(state[i].stateBuffer);
    }
}

//called on mouse down and key down
//stores the state of all tracks that could potentially be modified
//by this action so that we can push ones that actually were changed
//onto the undo stack
void ofxTimeline::collectStateBuffers(){
    
    if(!undoEnabled) return;
    
    vector<ofxTLTrack*> tracks = currentPage->getTracks();
    stateBuffers.clear();
    modifiedTracks.clear();
    for(int i = 0; i < tracks.size(); i++){
        ofxTLTrack* track = tracks[i];
        if(track->getSelectedItemCount() > 0 || track->isHovering() || track->hasFocus()){
			
            UndoItem ui;
            ui.track = track;
            ui.stateBuffer = track->getXMLRepresentation();
            stateBuffers.push_back(ui);
//			cout << "collecting state for " << track->getDisplayName() << endl;
//			cout << ui.stateBuffer << endl;
	
        }
    }
}

//go through the state buffers and see which tracks were actually modified
//push the collection of them onto the stack if there were any
void ofxTimeline::pushUndoStack(){
//    cout << "pushing undo stack" << endl;

    if(!undoEnabled) return;
    
    vector<UndoItem> undoCollection;
	set<ofxTLTrack*>::iterator trackit;
//    for(int i = 0; i < modifiedTracks.size(); i++){
	for(trackit = modifiedTracks.begin(); trackit != modifiedTracks.end(); trackit++){
        for(int buf = 0; buf < stateBuffers.size(); buf++){
            //this m
            if(*trackit == stateBuffers[buf].track){
//				cout << "modified state buffer for " << modifiedTracks[i]->getDisplayName() << endl;
                undoCollection.push_back(stateBuffers[buf]);
            }
        }
    }
	
    if(undoCollection.size() > 0){
        //remove any history that we've undone
        while(undoPointer < undoStack.size()){
            undoStack.pop_back();
        }
        undoStack.push_back(undoCollection);
        undoPointer = undoStack.size();
        
        //store the most recent state at the top of the queue
        vector<UndoItem> currentState;
		for(trackit = modifiedTracks.begin(); trackit != modifiedTracks.end(); trackit++){
            UndoItem ui;
            ui.track = *trackit;//modifiedTracks[i];
            ui.stateBuffer = (*trackit)->getXMLRepresentation();
            currentState.push_back(ui);
        }
        undoStack.push_back(currentState);
    }
	
}

void ofxTimeline::setMovePlayheadOnDrag(bool movePlayhead){
	movePlayheadOnDrag = movePlayhead;
}

bool ofxTimeline::getMovePlayheadOnDrag(){
	return movePlayheadOnDrag;
}

ofxTLPlaybackEventArgs ofxTimeline::createPlaybackEvent(){
	ofxTLPlaybackEventArgs args;
    args.sender = this;
	args.durationInFrames = timecode.frameForSeconds(durationInSeconds);
	args.durationInSeconds = durationInSeconds;
	args.currentTime = currentTime;
	args.currentFrame = getCurrentFrame();
	args.currentPercent = getPercentComplete();
	return args;
}

//internal elements call this when the value has changed,
//can call repeatedly without incurring saves
void ofxTimeline::flagUserChangedValue(){
	userChangedValue = true;
}

//this returns and clears the flag, generally call once per frame
bool ofxTimeline::getUserChangedValue(){
	bool hasChanged = userChangedValue;
    userChangedValue = false;
    return hasChanged;
}

void ofxTimeline::flagTrackModified(ofxTLTrack* track){
//	cout << "modified track " << track->getDisplayName() << endl;
	flagUserChangedValue();
    
    if(undoEnabled){
        modifiedTracks.insert(track);
    }
	
    unsavedChanges = true;
    if(autosave){
        track->save();
    }
}

bool ofxTimeline::hasUnsavedChanges(){
	return unsavedChanges;
}

void ofxTimeline::save(){
	for(int i = 0; i < pages.size(); i++){
        pages[i]->save();
    }
	zoomer->save();
	inoutTrack->save();
	unsavedChanges = false;
}

void ofxTimeline::play(){

    if(!isEnabled){
        return;
    }
	
	if(!getIsPlaying()){

		//commented out - always updating
//		if(!isOnThread){
//			ofAddListener(ofEvents().update, this, &ofxTimeline::update);
//		}
		if(timeControl != NULL){
			timeControl->play();
			return;
		}
		//if we are at the end and not looping, reset to the beginning
        if(isDone()){
            setPercentComplete(0.0);
        }
		
		isPlaying = true;
        currentTime = ofClamp(currentTime, getInTimeInSeconds(), getOutTimeInSeconds());
        playbackStartTime = timer.getAppTimeSeconds() - currentTime;
        playbackStartFrame = ofGetFrameNum() - timecode.frameForSeconds(currentTime);        
		ofxTLPlaybackEventArgs args = createPlaybackEvent();
		ofNotifyEvent(timelineEvents.playbackStarted, args);
	}
}

static bool menusRemoved = false;
void ofxTimeline::removeCocoaMenusFromGlut(string appName){

	#ifdef TARGET_OSX
	if(!menusRemoved){
		RemoveCocoaMenusFromGlut(appName);
		menusRemoved = true;
	}
	#endif
}

void ofxTimeline::stop(){

    if(!isEnabled){
        return;
    }

	if(getIsPlaying()){
		
//		if(!isOnThread){
//	        ofRemoveListener(ofEvents().update, this, &ofxTimeline::update);
//		}
		
		if(timeControl != NULL){
			timeControl->stop();
			return;
		}
		
        isPlaying = false;

        ofxTLPlaybackEventArgs args = createPlaybackEvent();
        ofNotifyEvent(timelineEvents.playbackEnded, args);
	}
}

void ofxTimeline::playSelectedTrack(){
	if(currentPage->getFocusedTrack() != NULL) currentPage->getFocusedTrack()->play();
}

void ofxTimeline::stopSelectedTrack(){
	if(currentPage->getFocusedTrack() != NULL) currentPage->getFocusedTrack()->stop();
}

bool ofxTimeline::togglePlaySelectedTrack(){
	if(currentPage->getFocusedTrack() != NULL) currentPage->getFocusedTrack()->togglePlay();
	return getIsPlaying();
}

bool ofxTimeline::togglePlay(){
	
    if(!isEnabled){
        return false;
    }
    
//    if(timeControl != NULL){
//        return timeControl->togglePlay();
//    }

	if(getIsPlaying()){
		stop();
	}
	else{
		play();
	}
	return getIsPlaying();
}

bool ofxTimeline::getIsPlaying(){
	return timeControl != NULL ? timeControl->getIsPlaying() : isPlaying;
}

void ofxTimeline::setCurrentFrame(int newFrame){
    currentTime = timecode.secondsForFrame(newFrame);
}

void ofxTimeline::setPercentComplete(float percent){
    currentTime = percent*durationInSeconds;
}

void ofxTimeline::setCurrentTimecode(string timecodeString){
    currentTime = timecode.secondsForTimecode(timecodeString);
}

void ofxTimeline::setHoverTime(unsigned long millisTime){
	ticker->setHoverTime(millisTime);
}

void ofxTimeline::setCurrentTimeSeconds(float time){
	currentTime = time;
}

void ofxTimeline::setCurrentTimeMillis(unsigned long millis){
	currentTime = millis/1000.;
}

void ofxTimeline::setFrameRate(float fps){
	timecode.setFPS(fps);    
}

void ofxTimeline::setFrameBased(bool frameBased){
    isFrameBased = frameBased;
}

bool ofxTimeline::getIsFrameBased(){
	return isFrameBased;
}

int ofxTimeline::getCurrentFrame(){
    return timecode.frameForSeconds(currentTime);
}

long ofxTimeline::getCurrentTimeMillis(){
    return currentTime*1000;
}

float ofxTimeline::getCurrentTime(){
	return currentTime;
}

float ofxTimeline::getPercentComplete(){
    return currentTime / durationInSeconds;
}

string ofxTimeline::getCurrentTimecode(){
    return timecode.timecodeForSeconds(currentTime);
}

long ofxTimeline::getQuantizedTime(unsigned long time, unsigned long step){
	double oneMeasure = 1000/(getBPM()/240.); // in milliseconds
	step = oneMeasure / step; // convert step to milliseconds
	unsigned long base = time / step;
	base = time % step > (step * 0.5) ? base + 1 : base; // round up or down
	return base * step;
}

void ofxTimeline::setInPointAtPlayhead(){
    setInPointAtSeconds(currentTime);
}
void ofxTimeline::setInPointAtPercent(float percent){
	inoutRange.min = ofClamp(percent, 0, inoutRange.max);
}
void ofxTimeline::setInPointAtSeconds(float time){
	setInPointAtPercent(time/durationInSeconds);	    
}
void ofxTimeline::setInPointAtFrame(int frame){
    setInPointAtPercent(timecode.secondsForFrame(frame) / durationInSeconds);
}
void ofxTimeline::setInPointAtMillis(long millis){
    setInPointAtPercent(millis / (1000. * durationInSeconds) );
}
void ofxTimeline::setInPointAtTimecode(string timecodeString){
    setInPointAtPercent(timecode.secondsForTimecode(timecodeString) / durationInSeconds);
}

void ofxTimeline::setOutPointAtPlayhead(){
    setOutPointAtSeconds(currentTime);
}
void ofxTimeline::setOutPointAtPercent(float percent){
	inoutRange.max = ofClamp(percent, inoutRange.min, 1.0);
}
void ofxTimeline::setOutPointAtFrame(float frame){
    setOutPointAtPercent(timecode.secondsForFrame(frame) / durationInSeconds);
}
void ofxTimeline::setOutPointAtSeconds(float time){
    setOutPointAtPercent(time/durationInSeconds);
}
void ofxTimeline::setOutPointAtMillis(long millis){
    setOutPointAtPercent(millis / (1000. * durationInSeconds) );
}
void ofxTimeline::setOutPointAtTimecode(string timecodeString){
    setOutPointAtPercent(timecode.secondsForTimecode(timecodeString) / durationInSeconds);    
}

void ofxTimeline::setInOutRange(ofRange inoutPercentRange){
    if(inoutPercentRange.min > inoutPercentRange.max) return;
	inoutRange = inoutPercentRange;
}

void ofxTimeline::setCurrentTimeToInPoint(){
    setPercentComplete(inoutRange.min);
}

void ofxTimeline::setCurrentTimeToOutPoint(){
    setPercentComplete(inoutRange.max);
}

void ofxTimeline::clearInOut(){
	setInOutRange(ofRange(0.0,1.0));    
}

ofRange ofxTimeline::getInOutRange(){
	return inoutRange;
}

ofLongRange ofxTimeline::getInOutRangeMillis(){
	return ofLongRange(getInTimeInMillis(), getOutTimeInMillis());
}

int ofxTimeline::getInFrame(){
	return timecode.frameForSeconds(getInTimeInSeconds());
}

int ofxTimeline::getOutFrame(){
    return timecode.frameForSeconds(getOutTimeInSeconds());
}

float ofxTimeline::getInTimeInSeconds(){
	return durationInSeconds*inoutRange.min;
}

float ofxTimeline::getOutTimeInSeconds(){
	return durationInSeconds*inoutRange.max;
}

long ofxTimeline::getInTimeInMillis(){
    return getInTimeInSeconds()*1000;
}

string ofxTimeline::getInPointTimecode(){
	return timecode.timecodeForSeconds(getInTimeInSeconds());
}

long ofxTimeline::getOutTimeInMillis(){
    return getOutTimeInSeconds()*1000;    
}

string ofxTimeline::getOutPointTimecode(){
	return timecode.timecodeForSeconds(getOutTimeInSeconds());    
}

bool ofxTimeline::toggleEnabled(){
	isEnabled = !isEnabled;
	return isEnabled;
}

void ofxTimeline::enable(){
    if(!isEnabled){
		isEnabled = true;
		enableEvents();
    }
}

void ofxTimeline::disable(){
    if(isEnabled){
        stop();
		isEnabled = false;
		disableEvents();
    }
}

//clears every element
//TODO how should this work with Undo??
void ofxTimeline::clear(){
	for(int i = 0; i < pages.size(); i++){
        pages[i]->clear();
    }
}

void ofxTimeline::reset(){ //gets rid of everything
	if(isOnThread){
		waitForThread(true);
	}
    stop();
    undoStack.clear();
    for(int i = 0; i < pages.size(); i++){ 
        delete pages[i];
    }
	
	tabs->clear();
    setInOutRange(ofRange(0,1.0));
    pages.clear();
    trackNameToPage.clear();
    currentPage = NULL;
    modalTrack = NULL;
    timeControl = NULL;
	addPage("Page One", true);
	if(isOnThread){
		startThread();
	}
}


void ofxTimeline::setDurationInFrames(int frames){
    setDurationInSeconds(timecode.secondsForFrame(frames));
}

void ofxTimeline::setDurationInSeconds(float seconds){
	//TODO: verify no elements are being truncated
    if(seconds <= 0.){
    	ofLogError("ofxTimeline::setDurationInSeconds") << " duraiton must set a positive number";
        return;
    }
	durationInSeconds = seconds;
	zoomer->setViewRange(zoomer->getSelectedRange());
}

void ofxTimeline::setDurationInMillis(unsigned long millis){
    setDurationInSeconds(millis/1000.);
}

void ofxTimeline::setDurationInTimecode(string timecodeString){
    float newDuration = timecode.secondsForTimecode(timecodeString);
    if(newDuration > 0){
	    setDurationInSeconds(newDuration);
    }
    else{
        ofLogError() << "ofxTimeline::setDurationInTimecode -- " << timecodeString << " is invalid, please use the format HH:MM:SS:MLS";
    }
}

int ofxTimeline::getDurationInFrames(){
    return timecode.frameForSeconds(durationInSeconds);
}

long ofxTimeline::getDurationInMilliseconds(){
	return durationInSeconds*1000;    //TODO: update to store currentTime in millis
}

float ofxTimeline::getDurationInSeconds(){
	return durationInSeconds;
}

string ofxTimeline::getDurationInTimecode(){
    return timecode.timecodeForSeconds(durationInSeconds);
}

void ofxTimeline::setAutosave(bool doAutosave){
	autosave = doAutosave;
}

void ofxTimeline::setOffset(ofVec2f newOffset){
    if(offset != newOffset){
        offset = newOffset;
        updatePagePositions();
        ofEventArgs args;
        ofNotifyEvent(timelineEvents.viewWasResized, args);
    }
}

void ofxTimeline::setLockWidthToWindow(bool lockWidth){
    lockWidthToWindow = lockWidth;
    if(width != ofGetWidth()){
        recalculateBoundingRects();
    }
}

void ofxTimeline::setEditableHeaders(bool headersEditable){
	headersAreEditable = headersEditable;
}

bool ofxTimeline::areHeadersEditable(){
	return headersAreEditable;
}

bool ofxTimeline::getLockWidthToWindow(){
    return lockWidthToWindow;
}

void ofxTimeline::setWidth(float newWidth){
    if(width != newWidth){
		if(newWidth != ofGetWidth()){
			lockWidthToWindow = false;
		}
        width = newWidth;
        updatePagePositions();
        ofEventArgs args;
        ofNotifyEvent(events().viewWasResized, args);
    }
}

void ofxTimeline::collapseAllTracks(){
	currentPage->collapseAllTracks();
}

ofRectangle ofxTimeline::getDrawRect(){
	return totalDrawRect;
}

ofVec2f ofxTimeline::getTopLeft(){
    return ofVec2f(totalDrawRect.x, totalDrawRect.y);
}

ofVec2f ofxTimeline::getTopRight(){
    return ofVec2f(totalDrawRect.x+totalDrawRect.width, totalDrawRect.y);
}

ofVec2f ofxTimeline::getBottomLeft(){
	return ofVec2f(totalDrawRect.x, totalDrawRect.y+totalDrawRect.height);    
}

ofVec2f ofxTimeline::getBottomRight(){
	return ofVec2f(totalDrawRect.x+totalDrawRect.width, totalDrawRect.y+totalDrawRect.height);    
}

void ofxTimeline::updatePagePositions(){
	if(isSetup){
		ofVec2f pageOffset = ofVec2f(offset.x, inoutTrack->getBottomEdge());
		for(int i = 0; i < pages.size(); i++){
			pages[i]->setContainer(pageOffset, width);
		}
		if(currentPage != NULL){
			currentPage->recalculateHeight();
		}
	}
}

void ofxTimeline::presentedModalContent(ofxTLTrack* newModalTrack){
    modalTrack = newModalTrack;
}

void ofxTimeline::dismissedModalContent(){
	modalTrack = NULL;    
}

void ofxTimeline::unselectAll(){
	currentPage->unselectAll();
}

void ofxTimeline::setBPM(float bpm) {
    ticker->setBPM(bpm);
}

float ofxTimeline::getBPM() {
	return ticker->getBPM();
}

bool ofxTimeline::toggleSnapToBPM(){
	snapToBPM = !snapToBPM;
    return snapToBPM;
}

void ofxTimeline::enableSnapToBPM(bool enableSnap){
    snapToBPM = enableSnap;
}

bool ofxTimeline::getSnapToBPM(){
    return snapToBPM;
}

bool ofxTimeline::toggleShowBPMGrid(){
    ticker->setDrawBPMGrid(!ticker->getDrawBPMGrid());		
	return ticker->getDrawBPMGrid();
}

void ofxTimeline::setShowBPMGrid(bool enableGrid){
    ticker->setDrawBPMGrid(enableGrid);
}

bool ofxTimeline::getShowBPMGrid(){
    return ticker->getDrawBPMGrid();
}

bool ofxTimeline::toggleSnapToOtherKeyframes(){
    snapToOtherElements = !snapToOtherElements;
    return snapToOtherElements;
}

void ofxTimeline::enableSnapToOtherKeyframes(bool enableSnapToOther){
    snapToOtherElements = enableSnapToOther;
}

bool ofxTimeline::getSnapToOtherElements(){
    return snapToOtherElements;
}

#pragma mark EVENTS
void ofxTimeline::enableEvents() {
	if (!usingEvents) {
		ofAddListener(ofEvents().mouseMoved, this, &ofxTimeline::mouseMoved);
		ofAddListener(ofEvents().mousePressed, this, &ofxTimeline::mousePressed);
		ofAddListener(ofEvents().mouseReleased, this, &ofxTimeline::mouseReleased);
		ofAddListener(ofEvents().mouseDragged, this, &ofxTimeline::mouseDragged);
//		ofAddListener(ofEvents().draw, this, &ofxTimeline::draw);

		ofAddListener(ofEvents().keyPressed, this, &ofxTimeline::keyPressed);
		ofAddListener(ofEvents().keyReleased, this, &ofxTimeline::keyReleased);
		ofAddListener(ofEvents().windowResized, this, &ofxTimeline::windowResized);
		
		usingEvents = true;
	}
}

void ofxTimeline::disableEvents() {
	if (usingEvents) {
		ofRemoveListener(ofEvents().mouseMoved, this, &ofxTimeline::mouseMoved);
		ofRemoveListener(ofEvents().mousePressed, this, &ofxTimeline::mousePressed);
		ofRemoveListener(ofEvents().mouseReleased, this, &ofxTimeline::mouseReleased);
		ofRemoveListener(ofEvents().mouseDragged, this, &ofxTimeline::mouseDragged);
		
		ofRemoveListener(ofEvents().keyPressed, this, &ofxTimeline::keyPressed);
		ofRemoveListener(ofEvents().keyReleased, this, &ofxTimeline::keyReleased);
		ofRemoveListener(ofEvents().windowResized, this, &ofxTimeline::windowResized);
		
		usingEvents = false;
	}
}

void ofxTimeline::mousePressed(ofMouseEventArgs& args){
    long millis = screenXToMillis(args.x);

    if(modalTrack != NULL){
    	modalTrack->mousePressed(args,millis);
    }
    else{
		bool focus = getDrawRect().inside(args.x, args.y);
		if(focus && !timelineHasFocus){
			currentPage->timelineGainedFocus();    
		}
		else if(!focus && timelineHasFocus){
			currentPage->timelineLostFocus();
		}
		timelineHasFocus = focus;
		if(timelineHasFocus){
			tabs->mousePressed(args);
			inoutTrack->mousePressed(args);
			ticker->mousePressed(args);
			currentPage->mousePressed(args,millis);
			zoomer->mousePressed(args);
		}
		else{
			unselectAll();
		}
		currentPage->setSnappingEnabled((snapToBPM || snapToOtherElements) && dragAnchorSet);
	}
	
    //collect state buffers after items are selected and focus is set
    collectStateBuffers();

}

void ofxTimeline::mouseMoved(ofMouseEventArgs& args){
    long millis = screenXToMillis(args.x);
    
    if(modalTrack != NULL){
    	modalTrack->_mouseMoved(args, millis);
        return;
    }
    
    inoutTrack->mouseMoved(args);
	ticker->mouseMoved(args);
	currentPage->mouseMoved(args, millis);
	zoomer->mouseMoved(args);
}

void ofxTimeline::mouseDragged(ofMouseEventArgs& args){
    long millis = screenXToMillis(args.x);
    
    if(modalTrack != NULL){
    	modalTrack->mouseDragged(args, false);
        return;
    }
    
	inoutTrack->mouseDragged(args);
	ticker->mouseDragged(args);
	currentPage->mouseDragged(args, millis);
	zoomer->mouseDragged(args);
}

void ofxTimeline::mouseReleased(ofMouseEventArgs& args){
    long millis = screenXToMillis(args.x);
    
    dragAnchorSet = false;

    if(modalTrack != NULL){
    	modalTrack->mouseReleased(args, millis);
	}
    else{
		inoutTrack->mouseReleased(args);
		ticker->mouseReleased(args);
		tabs->mouseReleased(args);
		currentPage->mouseReleased(args, millis);
		zoomer->mouseReleased(args);
	}
    
	pushUndoStack();
}

void ofxTimeline::keyPressed(ofKeyEventArgs& args){
	
    //cout << "key event " << args.key << " z? " << int('z') << " ctrl? " << ofGetModifierControlPressed() << " " << ofGetModifierShiftPressed() << " short cut? " << ofGetModifierShortcutKeyPressed() << endl;

	if(modalTrack != NULL){
		collectStateBuffers();
        modalTrack->keyPressed(args);
		pushUndoStack();
		return;
    }

	if(!timelineHasFocus) return;
	
	if(undoEnabled && ofGetModifierShortcutKeyPressed() && (args.key == 'z' || args.key == 'z'-96)){
		if(ofGetModifierShiftPressed()){
			redo();
		}
		else{
			undo();
		}
		return;
	}

	//collect the buffers before the command is sent becasue it's what modifies
    collectStateBuffers();
    
    
	if(ofGetModifierShortcutKeyPressed()){
		if(args.key == 'c' || args.key == 'c'-96){ //copy
			vector<string> copyattempt;
			currentPage->copyRequest(copyattempt);
			if(copyattempt.size() > 0){
				pasteboard = copyattempt;
			}
		}
		else if(args.key == 'x' || args.key == 'x'-96){ //cut
			vector<string> copyattempt;
			currentPage->cutRequest(copyattempt);
			if(copyattempt.size() > 0){
				pasteboard = copyattempt;
			}
		}
		else if(args.key == 'v' || args.key == 'v'-96){ //paste
			if (pasteboard.size() > 0) {
				currentPage->pasteSent(pasteboard);
			}				
		}
		else if(args.key == 'a' || args.key == 'a'-96){ //select all
			if(!ofGetModifierShiftPressed()){
				unselectAll();
			}
			currentPage->selectAll();						
		}
		else if(!autosave && unsavedChanges && (args.key == 's' || args.key == 's'-96) ){ //save
			save();
		}
	}
	else{
		if(args.key >= OF_KEY_LEFT && args.key <= OF_KEY_DOWN){
			ofVec2f nudgeAmount = ofGetModifierShiftPressed() ? getBigNudgePercent() : getNudgePercent();

			if(getTotalSelectedItems() == 0){
				if(args.key == OF_KEY_LEFT){
					if(getIsFrameBased()){
						currentTime -= timecode.secondsForFrame(1);
					}
					else{
						currentTime -= nudgeAmount.x*getDurationInSeconds();
					}
				}
				if(args.key == OF_KEY_RIGHT){
					if(getIsFrameBased()){
						currentTime += timecode.secondsForFrame(1);
					}
					else{
						currentTime += nudgeAmount.x*getDurationInSeconds();
					}
				}
			}
			else{
				if(args.key == OF_KEY_UP){
					nudgeAmount.x = 0;
				}
				if(args.key == OF_KEY_DOWN){
					nudgeAmount.x = 0;
					nudgeAmount.y = -nudgeAmount.y;
				}
				if(args.key == OF_KEY_RIGHT){
					nudgeAmount.y = 0;
				}
				if(args.key == OF_KEY_LEFT){
					nudgeAmount.x = -nudgeAmount.x;
					nudgeAmount.y = 0;
				}
				currentPage->nudgeBy(nudgeAmount);
			}
		}
		
		ticker->keyPressed(args);
		currentPage->keyPressed(args);
		zoomer->keyPressed(args);
	}
	
	pushUndoStack();
}

void ofxTimeline::keyReleased(ofKeyEventArgs& args){
	//Not reliably called when COMMAND is pressed on OS X
}

void ofxTimeline::windowResized(ofResizeEventArgs& args){
    ofNotifyEvent(events().viewWasResized, args, this);
}

void ofxTimeline::exit(ofEventArgs& args){
//	stop();
//    undoStack.clear();
//    for(int i = 0; i < pages.size(); i++){
//        delete pages[i];
//    }
//	tabs->clear();
//    pages.clear();
//    trackNameToPage.clear();
//    currentPage = NULL;
//    modalTrack = NULL;
//    timeControl = NULL;
//
	if(isOnThread){
		ofLogNotice("ofxTimeline::exit") << "waiting for thread" << endl;
		waitForThread(true);
	}
	
}

#pragma mark DRAWING
void ofxTimeline::viewWasResized(ofEventArgs& args){
	recalculateBoundingRects();
}

void ofxTimeline::recalculateBoundingRects(){
    if(lockWidthToWindow){
        width = ofGetWidth();
    }
    
	if(pages.size() > 1){
		tabs->setDrawRect(ofRectangle(offset.x, offset.y, width, TAB_HEIGHT));
	}
	else{
		tabs->setDrawRect(ofRectangle(offset.x, offset.y, width, 0));
	}
    
    ticker->setDrawRect( ofRectangle(offset.x, tabs->getBottomEdge(), width, showTicker ? TICKER_HEIGHT : 0) );
    inoutTrack->setDrawRect( ofRectangle(offset.x, ticker->getBottomEdge(), width, showInoutControl ? INOUT_HEIGHT : 0) );
    updatePagePositions();
	zoomer->setDrawRect(ofRectangle(offset.x, currentPage->getBottomEdge(), width, showZoomer ? ZOOMER_HEIGHT : 0));
    inoutTrack->setPageRectangle(currentPage->getDrawRect());
	ofRectangle tickerRect = ofRectangle(offset.x, ticker->getDrawRect().y,
                                        width, currentPage->getBottomEdge()-ticker->getDrawRect().y);
	ticker->setTotalDrawRect(tickerRect);		
	totalDrawRect = ofRectangle(offset.x, offset.y, width, zoomer->getDrawRect().y+zoomer->getDrawRect().height - offset.y);
}


void ofxTimeline::pageChanged(ofxTLPageEventArgs& args){
	for(int i = 0; i < pages.size(); i++){
		if(pages[i]->getName() == args.currentPageName){
			if(currentPage != NULL){
				currentPage->unselectAll();
			}
			currentPage = pages[i];
			ofEventArgs args;
			ofNotifyEvent(events().viewWasResized, args);
			return;
		}
	}
		
	ofLogError("ofxTimeline -- Tabbed to nonexistence page " + args.currentPageName);
}

void ofxTimeline::setLoopType(ofLoopType newType){
	loopType = newType;
}

ofLoopType ofxTimeline::getLoopType(){
	return loopType;
}

bool ofxTimeline::isDone(){
	return getPercentComplete() >= inoutRange.max && getLoopType() == OF_LOOP_NONE;   
}

void ofxTimeline::update(ofEventArgs& updateArgs){
	if(!isOnThread){
		updateTime();
	}
}

void ofxTimeline::threadedFunction(){
	while(isThreadRunning()){
		updateTime();
		ofSleepMillis(1);
	}
}

void ofxTimeline::updateTime(){
	
	if(getIsPlaying()){
		if(timeControl == NULL){
			if(isFrameBased){
				currentTime = timecode.secondsForFrame(ofGetFrameNum() - playbackStartFrame);
			}
			else {
				currentTime = timer.getAppTimeSeconds() - playbackStartTime;
			}
			checkLoop();
		}
	}
	
	checkEvents();
}

void ofxTimeline::checkEvents(){
	for(int i = 0; i < pages.size(); i++){
		pages[i]->update();
	}
}

void ofxTimeline::checkLoop(){
	if(currentTime < durationInSeconds*inoutRange.min){
        currentTime = durationInSeconds*inoutRange.min;
        playbackStartTime = timer.getAppTimeSeconds() - currentTime;
        playbackStartFrame = ofGetFrameNum() - timecode.frameForSeconds(currentTime);
    }
    
    if(currentTime >= durationInSeconds*inoutRange.max){
        if(loopType == OF_LOOP_NONE){
            currentTime = durationInSeconds*inoutRange.max;
            stop();
        }
        else if(loopType == OF_LOOP_NORMAL) {
            currentTime = durationInSeconds*inoutRange.min + (currentTime - durationInSeconds*inoutRange.max);
            playbackStartFrame += getDurationInFrames()  * inoutRange.span();
            playbackStartTime  += getDurationInSeconds() * inoutRange.span();
            ofxTLPlaybackEventArgs args = createPlaybackEvent();
            ofNotifyEvent(events().playbackLooped, args);
        }
    }
}

//void ofxTimeline::draw(ofEventArgs& args){
void ofxTimeline::draw(){

	if(isSetup && isShowing){
		ofPushStyle();

		glPushAttrib(GL_ENABLE_BIT);
		glDisable(GL_DEPTH_TEST);
        ofDisableLighting();
		ofEnableAlphaBlending();
		ofSetColor(255*.15);
		ofRect(totalDrawRect);
		ofSetColor(255);
		
		if (pages.size() > 1) {
			tabs->draw();			
		}
        
		ofPushStyle();
		currentPage->draw();
		if(showZoomer)zoomer->_draw();
        
		//draw these because they overlay the rest of the timeline with info
        ticker->_draw();
		inoutTrack->_draw();
        ofPopStyle();
		
		if(modalTrack != NULL){
			modalTrack->drawModalContent();
		}
			
		glPopAttrib();
		ofPopStyle();
	}
}

#pragma mark ELEMENT CREATORS/GETTERS/SETTERS
void ofxTimeline::addPage(string pageName, bool makeCurrent){
	if(pageName == ""){
		ofLogError("ofxTimeline -- Cannot add page with an empty name.");
		return;
	}
	
	for(int i = 0; i < pages.size(); i++){
		if(pageName == pages[i]->getName()){
			ofLogError("ofxTimeline -- Page " + pageName + " already exists");
			return;
		}
	}
    
	ofxTLPage* newPage = new ofxTLPage();
    newPage->timeline = this;
	newPage->setName(pageName);
	newPage->setup();
	newPage->setZoomBounds(zoomer->getViewRange());
	newPage->setTicker(ticker);
    
	pages.push_back(newPage);
	tabs->addPage(pageName);

	if(makeCurrent){
		tabs->selectPage(pageName);
	}
}

void ofxTimeline::setPageName(string newName){
	tabs->changeName(currentPage->getName(), newName);
	currentPage->setName( newName );
	currentPage->loadTrackPositions();
}

void ofxTimeline::setPageName(string newName, int index){
	if(index < 0 || index >= pages.size()){
        ofLogError("ofxTimeline::setPageName -- index out of bounds");
        return;
    }
    tabs->changeName(pages[index]->getName(), newName);
    pages[index]->setName(newName);
    pages[index]->loadTrackPositions();
}

void ofxTimeline::setCurrentPage(string pageName){
	tabs->selectPage(pageName);
}

void ofxTimeline::setCurrentPage(int index){
	tabs->selectPage(index);
}

int ofxTimeline::getTotalSelectedItems(){
    int totalSelected = 0;
    vector<ofxTLTrack*> tracks = currentPage->getTracks();
    for(int i = 0; i < tracks.size(); i++){
        totalSelected += tracks[i]->getSelectedItemCount();
    }
    return totalSelected;
}

unsigned long ofxTimeline::getEarliestTime(){
	unsigned long earliestTime = LONG_MAX;
    for(int i = 0; i < pages.size(); i++){
		for(int t = 0; t < pages[i]->getTracks().size(); t++){
			earliestTime = MIN(earliestTime,pages[i]->getTracks()[t]->getEarliestTime());
		}
    }
	return earliestTime;
}

unsigned long ofxTimeline::getLatestTime(){
	unsigned long latestTime = 0;
    for(int i = 0; i < pages.size(); i++){
		for(int t = 0; t < pages[i]->getTracks().size(); t++){
			latestTime = MAX(latestTime,pages[i]->getTracks()[t]->getLatestTime());
		}
    }
	return latestTime;	
}

unsigned long ofxTimeline::getEarliestSelectedTime(){
	unsigned long earliestTime = LONG_MAX;
    for(int i = 0; i < pages.size(); i++){
		for(int t = 0; t < pages[i]->getTracks().size(); t++){
			earliestTime = MIN(earliestTime,pages[i]->getTracks()[t]->getEarliestSelectedTime());
		}
    }
	return earliestTime;	
}

unsigned long ofxTimeline::getLatestSelectedTime(){
	unsigned long latestTime = 0;
    for(int i = 0; i < pages.size(); i++){
		for(int t = 0; t < pages[i]->getTracks().size(); t++){
			latestTime = MAX(latestTime,pages[i]->getTracks()[t]->getLatestSelectedTime());
//			cout << "latest selected time is now " << latestTime << endl;
		}
    }
	return latestTime;
}

bool ofxTimeline::isModal(){
    return modalTrack != NULL;
}

ofxTLTrack* ofxTimeline::getModalTrack(){
    return modalTrack;
}

void ofxTimeline::setTimecontrolTrack(ofxTLTrack* track){
    timeControl = track;
}

ofxTLTrack* ofxTimeline::getTimecontrolTrack(){
    return timeControl;
}

ofxTLZoomer* ofxTimeline::getZoomer(){
	return zoomer;
}

//can be used to add custom elements
void ofxTimeline::addTrack(string trackName, ofxTLTrack* track){
	if(trackNameToPage[trackName] != NULL){
        ofLogError("ofxTimeline::addTrack") << " Adding dupliciate track name " << trackName;
    }
	track->setTimeline( this );
	track->setName( trackName );
	currentPage->addTrack(trackName, track);	
	trackNameToPage[trackName] = currentPage;
	ofEventArgs args;
	ofNotifyEvent(events().viewWasResized, args);
}

//adding tracks always adds to the current page
ofxTLLFO* ofxTimeline::addLFO(string trackName, ofRange valueRange, float defaultValue){
    string uniqueName = confirmedUniqueName(trackName);
    return addLFO(uniqueName, nameToXMLName(uniqueName), valueRange, defaultValue);
}

ofxTLLFO* ofxTimeline::addLFO(string trackName, string xmlFileName, ofRange valueRange, float defaultValue){
	ofxTLLFO* newLFO = new ofxTLLFO();
	newLFO->setCreatedByTimeline(true);
	newLFO->setValueRange(valueRange, defaultValue);
	newLFO->setXMLFileName(xmlFileName);
	addTrack(confirmedUniqueName(trackName), newLFO);
	return newLFO;
}

ofxTLCurves* ofxTimeline::addCurves(string trackName, ofRange valueRange, float defaultValue){
    string uniqueName = confirmedUniqueName(trackName);
    return addCurves(uniqueName, nameToXMLName(uniqueName), valueRange, defaultValue);
}

ofxTLCurves* ofxTimeline::addCurves(string trackName, string xmlFileName, ofRange valueRange, float defaultValue){
	ofxTLCurves* newCurves = new ofxTLCurves();
	newCurves->useBinarySave = curvesUseBinary;
	newCurves->setCreatedByTimeline(true);
	newCurves->setValueRange(valueRange, defaultValue);
	newCurves->setXMLFileName(xmlFileName);
	addTrack(confirmedUniqueName(trackName), newCurves);
	return newCurves;
}

float ofxTimeline::getValue(string trackName, float atTime){
	if(!hasTrack(trackName)){
		ofLogError("ofxTimeline -- Couldn't find track " + trackName);
		return 0.0;
	}
	ofxTLCurves* curves = (ofxTLCurves*)trackNameToPage[trackName]->getTrack(trackName);
	return curves->getValueAtTimeInMillis(atTime*1000);
}

float ofxTimeline::getValue(string trackName){
	if(!hasTrack(trackName)){
		ofLogError("ofxTimeline -- Couldn't find track " + trackName);
		return 0.0;
	}
	ofxTLCurves* curves = (ofxTLCurves*)trackNameToPage[trackName]->getTrack(trackName);
	return curves->getValue();
}

float ofxTimeline::getValue(string trackName, int atFrame){
    return getValue(trackName, timecode.secondsForFrame(atFrame));
}

bool ofxTimeline::hasTrack(string trackName){
	return trackNameToPage.find(trackName) != trackNameToPage.end();
}

ofxTLTrack* ofxTimeline::getTrack(string trackName){
	if(!hasTrack(trackName)){
		ofLogError("ofxTimeline -- Couldn't find track " + trackName);
		return NULL;
	}
	return trackNameToPage[trackName]->getTrack(trackName);
}

ofxTLSwitches* ofxTimeline::addSwitches(string trackName){
    string uniqueName = confirmedUniqueName(trackName);
	return addSwitches(uniqueName, nameToXMLName(uniqueName));
}

ofxTLSwitches* ofxTimeline::addSwitches(string trackName, string xmlFileName){
	ofxTLSwitches* newSwitches = new ofxTLSwitches();
	newSwitches->setCreatedByTimeline(true);
	newSwitches->setXMLFileName(xmlFileName);
	addTrack(confirmedUniqueName(trackName), newSwitches);
	return newSwitches;
}

bool ofxTimeline::isSwitchOn(string trackName, float atTime){
	if(!hasTrack(trackName)){
		ofLogError("ofxTimeline -- Couldn't find switcher track " + trackName);
		return false;
	}
	
	ofxTLSwitches* switches = (ofxTLSwitches*)trackNameToPage[trackName]->getTrack(trackName);
    return switches->isOnAtPercent(atTime/durationInSeconds);
}

bool ofxTimeline::isSwitchOn(string trackName){
	if(!hasTrack(trackName)){
		ofLogError("ofxTimeline -- Couldn't find switcher track " + trackName);
		return false;
	}
	
	ofxTLSwitches* switches = (ofxTLSwitches*)trackNameToPage[trackName]->getTrack(trackName);
	return switches->isOn();
//    return isSwitchOn(trackName, currentTime);
}

bool ofxTimeline::isSwitchOn(string trackName, int atFrame){
	return isSwitchOn(trackName, timecode.secondsForFrame(atFrame));	
}

ofxTLBangs* ofxTimeline::addBangs(string trackName){
    string uniqueName = confirmedUniqueName(trackName);
 	return addBangs(uniqueName, nameToXMLName(uniqueName));   
}

ofxTLBangs* ofxTimeline::addBangs(string trackName, string xmlFileName){
	ofxTLBangs* newBangs = new ofxTLBangs();
	newBangs->setCreatedByTimeline(true);
	newBangs->setXMLFileName(xmlFileName);
	addTrack(confirmedUniqueName(trackName), newBangs);
	return newBangs;
}

ofxTLFlags* ofxTimeline::addFlags(string trackName){
    string uniqueName = confirmedUniqueName(trackName);
    return addFlags(uniqueName, nameToXMLName(uniqueName));
}

ofxTLFlags* ofxTimeline::addFlags(string trackName, string xmlFileName){
    ofxTLFlags* newFlags = new ofxTLFlags();
	newFlags->setCreatedByTimeline(true);
	newFlags->setXMLFileName(xmlFileName);
	addTrack(confirmedUniqueName(trackName), newFlags);
	return newFlags;
}

ofxTLColorTrack* ofxTimeline::addColors(string trackName){
	string uniqueName = confirmedUniqueName(trackName);
	return addColorsWithPalette(uniqueName, nameToXMLName(uniqueName), defaultPalettePath);
}

ofxTLColorTrack* ofxTimeline::addColors(string name, string xmlFileName){
	return addColorsWithPalette(confirmedUniqueName(name), xmlFileName, defaultPalettePath);
}

ofxTLColorTrack* ofxTimeline::addColorsWithPalette(string trackName, ofImage& palette){
	string uniqueName = confirmedUniqueName(trackName);
	return addColorsWithPalette(uniqueName, nameToXMLName(uniqueName), palette);
}

ofxTLColorTrack* ofxTimeline::addColorsWithPalette(string trackName, string palettePath){
	string uniqueName = confirmedUniqueName(trackName);
	return addColorsWithPalette(uniqueName, nameToXMLName(uniqueName), palettePath);
}

ofxTLColorTrack* ofxTimeline::addColorsWithPalette(string trackName, string xmlFileName, ofImage& palette){
	ofxTLColorTrack* newColors = new ofxTLColorTrack();
	newColors->setCreatedByTimeline(true);
	newColors->setXMLFileName(xmlFileName);
	newColors->loadColorPalette(palette);
	addTrack(confirmedUniqueName(trackName), newColors);
	return newColors;
}

ofxTLColorTrack* ofxTimeline::addColorsWithPalette(string trackName, string xmlFileName, string palettePath){
	ofxTLColorTrack* newColors = new ofxTLColorTrack();
	newColors->setCreatedByTimeline(true);
	newColors->setXMLFileName(xmlFileName);
	newColors->loadColorPalette(palettePath);
	addTrack(confirmedUniqueName(trackName), newColors);
	return newColors;
}

ofColor ofxTimeline::getColor(string trackName){
	if(!hasTrack(trackName)){
		ofLogError("ofxTimeline -- Couldn't find color track " + trackName);
		return ofColor(0,0,0);
	}
	ofxTLColorTrack* colors = (ofxTLColorTrack*)trackNameToPage[trackName]->getTrack(trackName);
	return colors->getColor();
}

ofColor ofxTimeline::getColorAtPercent(string trackName, float percent){
	return getColorAtMillis(trackName, percent*getDurationInMilliseconds());
}

ofColor ofxTimeline::getColorAtSecond(string trackName, float second){
	return getColorAtMillis(trackName, second*1000);
}

ofColor ofxTimeline::getColorAtMillis(string trackName, unsigned long millis){
	if(!hasTrack(trackName)){
	   ofLogError("ofxTimeline -- Couldn't find color track " + trackName);
		return ofColor(0,0,0);
	}
	
	ofxTLColorTrack* colors = (ofxTLColorTrack*)trackNameToPage[trackName]->getTrack(trackName);
	return colors->getColorAtMillis(millis);
}

string ofxTimeline::getDefaultColorPalettePath(){
	return defaultPalettePath;
}

//*** IMAGE SEQUENCE DOESN'T WORK **///
ofxTLImageSequence* ofxTimeline::addImageSequence(string trackName){
	ofFileDialogResult result = ofSystemLoadDialog("Load Sequence", true);
	if(result.bSuccess && ofDirectory::doesDirectoryExist(result.filePath, false)){
		return addImageSequence(trackName, result.getPath());
	}
	return NULL;
}

ofxTLImageSequence* ofxTimeline::addImageSequence(string trackName, string directory){
	ofxTLImageSequence*	newImageSequence = new ofxTLImageSequence();
	newImageSequence->setCreatedByTimeline(true);
	newImageSequence->loadSequence(directory);
	addTrack(confirmedUniqueName(trackName), newImageSequence);
	return newImageSequence;	
}


ofImage* ofxTimeline::getImage(string trackName){
	return NULL;
}

ofImage* ofxTimeline::getImage(string trackName, float atTime){
	return NULL;
}

ofImage* ofxTimeline::getImage(string trackName, int atFrame){
	return NULL;
}

ofxTLVideoTrack* ofxTimeline::addVideoTrack(string trackName){
	return addVideoTrack(trackName, "");
}

ofxTLVideoTrack* ofxTimeline::addVideoTrack(string trackName, string videoPath){
	ofxTLVideoTrack* videoTrack = new ofxTLVideoTrack();
	videoTrack->setCreatedByTimeline(true);
	addTrack(confirmedUniqueName(trackName), videoTrack);
	if(videoPath != ""){
	    if(!videoTrack->load(videoPath)){
        	ofLogError("ofxTimeline::addVideoTrack -- video " + videoPath + " failed to load");
		}
		else{
			//make time control by default
			setTimecontrolTrack(videoTrack);
			setFrameRate(videoTrack->getPlayer()->getTotalNumFrames()/videoTrack->getPlayer()->getDuration());
			setDurationInFrames(videoTrack->getPlayer()->getTotalNumFrames());
		}
    }
    return videoTrack;
}

ofxTLVideoTrack* ofxTimeline::getVideoTrack(string videoTrackName){
	return (ofxTLVideoTrack*)getTrack(videoTrackName);
}

ofPtr<ofVideoPlayer> ofxTimeline::getVideoPlayer(string videoTrackName){
    ofxTLVideoTrack* track = getVideoTrack(videoTrackName);
    if(track == NULL){
        ofLogError("ofxTimeline::getPlayer -- video player is null");
        return ofPtr<ofVideoPlayer>(); //null ptr
    }
    return track->getPlayer();
}

ofxTLTrackHeader* ofxTimeline::getTrackHeader(string trackName){
    return getTrackHeader(getTrack(name));    
}

ofxTLTrackHeader* ofxTimeline::getTrackHeader(ofxTLTrack* track){
    return trackNameToPage[track->getName()]->getTrackHeader(track);
}

void ofxTimeline::bringTrackToTop(string trackName){
    bringTrackToTop(getTrack(trackName));
}

void ofxTimeline::bringTrackToTop(ofxTLTrack* track){
    if(track != NULL){
		trackNameToPage[track->getName()]->bringTrackToTop(track);
		ofEventArgs args;
		ofNotifyEvent(events().viewWasResized, args);
    }
}

void ofxTimeline::bringTrackToBottom(string trackName){
	bringTrackToBottom(getTrack(trackName));    
}

void ofxTimeline::bringTrackToBottom(ofxTLTrack* track){
    if(track != NULL){
		trackNameToPage[track->getName()]->bringTrackToBottom(track);
		ofEventArgs args;
		ofNotifyEvent(events().viewWasResized, args);		
    }
}

void ofxTimeline::removeTrack(string name){
    ofxTLTrack* track = getTrack(name);
    if(track == NULL){
        ofLogError() << "ofxTimeline::removeTrack -- Could not find track " << name << " to remove " << endl;
        return;
	}
    if(track == modalTrack){
        modalTrack = NULL;
    }
	if(track == (ofxTLTrack*)timeControl){
		timeControl =  NULL;
	}
    //quick fix for now -- we need to have Undo and Delete track work together
    //but to prevent crashes, let's just go through the undo queue and remove any items that have to do with this track
    for(int i = 0; i < undoStack.size(); i++){
        for(int q = undoStack[i].size()-1; q >= 0; q--){
			if(undoStack[i][q].track == track){
                undoStack[i].erase(undoStack[i].begin() + q);
                cout << "temporary fix -- deleting undo queue element for track " << track->getName() << endl;
            }
        }
    }

    trackNameToPage[name]->removeTrack(track);
    trackNameToPage.erase(name);
	ofEventArgs args;
	ofNotifyEvent(events().viewWasResized, args);
}

void ofxTimeline::removeTrack(ofxTLTrack* track){
    removeTrack(track->getName()); //always delete by name to ensure the track is in this timeline
}

ofxTLEvents& ofxTimeline::events(){
    return timelineEvents;
}

ofxTimecode& ofxTimeline::getTimecode(){
    return timecode;
}

ofxMSATimer& ofxTimeline::getTimer(){
	return timer;
}

vector<ofxTLPage*>& ofxTimeline::getPages(){
    return pages;
}

string ofxTimeline::formatTime(float seconds){
    return timecode.timecodeForSeconds(seconds);
}

string ofxTimeline::formatTime(unsigned long millis){
    return timecode.timecodeForMillis(millis);
}

string ofxTimeline::nameToXMLName(string trackName){
    string xmlName = name + "_" + trackName;
    ofStringReplace(xmlName, " ", "_");
    ofStringReplace(xmlName, ":", "_");
    ofStringReplace(xmlName, "/", "_");
    ofStringReplace(xmlName, "\\", "_");
	xmlName += ".xml";
	return xmlName;	    
}

string ofxTimeline::confirmedUniqueName(string name){
    string uniqueName = name;
    int attempts = 0;
	while(trackNameToPage.find(uniqueName) != trackNameToPage.end()){
		uniqueName = name + "_" + ofToString(attempts++);
    }
    return uniqueName;
}



void ofxTimeline::setDragTimeOffset(unsigned long millisecondOffset){

	dragMillsecondOffset = millisecondOffset;

    currentPage->setDragOffsetTime(dragMillsecondOffset);
	dragAnchorSet = true;
}

//this can be called from track classes and will cancel snapping
void ofxTimeline::cancelSnapping(){
	dragAnchorSet = false;
}

long ofxTimeline::getDragTimeOffset(){
	return dragAnchorSet ? dragMillsecondOffset : 0.;
}

ofVec2f ofxTimeline::getNudgePercent(){
	return ofVec2f(zoomer->getViewRange().span()*.001, .001);
}

ofVec2f ofxTimeline::getBigNudgePercent(){
	return ofVec2f(zoomer->getViewRange().span()*.02, 0.02);	
}

long ofxTimeline::screenXToMillis(float x){
	return screenXtoNormalizedX(x) * durationInSeconds * 1000;
}

float ofxTimeline::millisToScreenX(long millis){
    return normalizedXtoScreenX(millis/(durationInSeconds*1000));
}

float ofxTimeline::screenXtoNormalizedX(float x){
    return screenXtoNormalizedX(x, zoomer->getViewRange());
}

float ofxTimeline::normalizedXtoScreenX(float x){
    return normalizedXtoScreenX(x, zoomer->getViewRange());
}

float ofxTimeline::screenXtoNormalizedX(float x, ofRange outputRange){
	return ofMap(x, getDrawRect().getMinX(), getDrawRect().getMaxX(), outputRange.min, outputRange.max, false);
}

float ofxTimeline::normalizedXtoScreenX(float x, ofRange inputRange){
	return ofMap(x, inputRange.min, inputRange.max, getDrawRect().getMinX(), getDrawRect().getMaxX(), false);
}
