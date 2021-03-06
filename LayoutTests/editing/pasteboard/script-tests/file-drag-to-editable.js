description('Tests that dragging a file into an editable area does not insert a filename.');

var editableArea = document.createElement('div');
editableArea.contentEditable = true;
editableArea.style.background = 'green';
editableArea.style.height = '100px';
editableArea.style.width = '100px';
// Important that we put this at the top of the doc so that logging does not cause it to go out of view (where it can't be dragged to)
document.body.insertBefore(editableArea, document.body.firstChild);

function moveMouseToCenterOfElement(element)
{
    var centerX = element.offsetLeft + element.offsetWidth / 2;
    var centerY = element.offsetTop + element.offsetHeight / 2;
    eventSender.mouseMoveTo(centerX, centerY);
}

function dragFilesOntoEditableArea(files)
{
    eventSender.beginDragWithFiles(files);
    moveMouseToCenterOfElement(editableArea);
    eventSender.mouseUp();
}

function runTest()
{
    window.onbeforeunload = function() {
        shouldBeTrue('successfullyParsed');
        debug('<br /><span class="pass">TEST COMPLETE</span>');

        // Don't remove the editable node, since we want to make sure there no stray file URLs were
        // inserted during the drop.

        layoutTestController.notifyDone();

        window.onbeforeunload = null;
    };
    dragFilesOntoEditableArea(['DRTFakeFile']);
    testFailed('The test should have resulted in navigation');
}

var successfullyParsed = true;

if (window.eventSender) {
    runTest();
} else {
    testFailed('This test is not interactive, please run using DumpRenderTree');
}
