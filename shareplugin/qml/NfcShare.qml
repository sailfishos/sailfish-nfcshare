import QtQuick 2.0
import Sailfish.Silica 1.0
import org.sailfishos.nfcshare 1.0

Item {
    id: thisItem

    property var shareAction

    property var _content: (shareAction && 'resources' in shareAction && shareAction.resources.length > 0) ? shareAction.resources[0] : undefined
    property string _text: ('data' in _content) ? _content.data : ('status' in _content) ? _content.status : ""

    width: parent.width
    height: 2 * Theme.itemSizeHuge + Theme.paddingSmall // Close enough to the default sharing dialog size

    NfcShare {
        id: nfcShare

        text: thisItem.visible ? _text : ""
        onDone: shareAction.done()
    }

    Item {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: errorMessage.visible ? errorMessage.top : progress.top
        }

        Image {
            // Older versions of Sailfish OS don't have icon-l-nfc
            readonly property string _fallbackSource: "image://theme/icon-m-nfc"

            anchors.centerIn: parent
            source: "image://theme/icon-l-nfc"

            onStatusChanged: {
                if (status == Image.Error && source != _fallbackSource) {
                    source = _fallbackSource
                    sourceSize = Qt.size(Theme.itemSizeLarge, Theme.itemSizeLarge)
                }
            }
        }
    }

    ProgressBar {
        id: progress

        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            bottomMargin: Theme.paddingLarge
        }
        indeterminate: !nfcShare.bytesTransferred
        maximumValue: nfcShare.bytesTotal
        value: nfcShare.bytesTransferred
        opacity: (nfcShare.ready && !nfcShare.done && !nfcShare.tooMuchData) ? 1 : 0
    }

    Label {
        id: errorMessage

        x: Theme.horizontalPageMargin
        width: parent.width - 2 * x
        anchors {
            bottom: parent.bottom
            bottomMargin: Theme.paddingLarge
        }
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.highlightColor
        visible: nfcShare.tooMuchData
        //: This text is shown when the amount of data to share exceeds the limit
        //% "Too much data to share via NFC"
        text: qsTrId("nfcshare-la-too-much-data")
    }
}
